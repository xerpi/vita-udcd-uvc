#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/udcd.h>
#include <psp2kern/display.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>
#include <string.h>
#include "usb_descriptors.h"
#include "uvc.h"

#ifdef DEBUG

#include "log.h"
#include "draw.h"
#include "console.h"

#define LOG(s, ...) \
	do { \
		char __buffer[128]; \
		snprintf(__buffer, sizeof(__buffer), s, ##__VA_ARGS__); \
		/*LOG_TO_FILE(__buffer);*/ \
		console_print(__buffer); \
	} while (0)
#else
#define LOG(...) (void)0
#endif

#define ALIGN(x, a)			(((x) + ((a) - 1)) & ~((a) - 1))
#define UNUSED(x)			((void)x)

#define UVC_DRIVER_NAME			"VITAUVC00"
#define UVC_USB_PID			0x1337

#define MAX_UVC_VIDEO_FRAME_SIZE	VIDEO_FRAME_SIZE_NV12(1280, 720)

#define UVC_PAYLOAD_HEADER_SIZE		16
#define UVC_PAYLOAD_SIZE(frame_size)	(UVC_PAYLOAD_HEADER_SIZE + (frame_size))
#define MAX_UVC_PAYLOAD_TRANSFER_SIZE	UVC_PAYLOAD_SIZE(MAX_UVC_VIDEO_FRAME_SIZE)

int ksceOledDisplayOn();
int ksceOledDisplayOff();
int ksceOledGetBrightness();
int ksceOledSetBrightness(int brightness);

int ksceLcdDisplayOn();
int ksceLcdDisplayOff();
int ksceLcdGetBrightness();
int ksceLcdSetBrightness(int brightness);

struct uvc_frame {
	unsigned char header[UVC_PAYLOAD_HEADER_SIZE];
	unsigned char data[];
} __attribute__((packed));

static const struct uvc_streaming_control uvc_probe_control_setting_default = {
	.bmHint				= 0,
	.bFormatIndex			= FORMAT_INDEX_UNCOMPRESSED_NV12,
	.bFrameIndex			= 1,
	.dwFrameInterval		= FPS_TO_INTERVAL(60),
	.wKeyFrameRate			= 0,
	.wPFrameRate			= 0,
	.wCompQuality			= 0,
	.wCompWindowSize		= 0,
	.wDelay				= 0,
	.dwMaxVideoFrameSize		= MAX_UVC_VIDEO_FRAME_SIZE,
	.dwMaxPayloadTransferSize	= MAX_UVC_PAYLOAD_TRANSFER_SIZE,
	.dwClockFrequency		= 0,
	.bmFramingInfo			= 0,
	.bPreferedVersion		= 1,
	.bMinVersion			= 0,
	.bMaxVersion			= 0,
};

static struct uvc_streaming_control uvc_probe_control_setting;

static struct {
	unsigned char buffer[64];
	SceUdcdEP0DeviceRequest ep0_req;
} pending_recv;

static SceUID uvc_thread_id;
static SceUID uvc_event_flag_id;
static int uvc_thread_run;
static int stream;

static SceUID uvc_frame_buffer_uid = -1;
static struct uvc_frame *uvc_frame_buffer_addr;
SceUID uvc_frame_req_evflag;

static int uvc_frame_init(unsigned int size);
static int uvc_frame_term();

#if defined(DISPLAY_OFF_OLED) || defined(DISPLAY_OFF_LCD)
static int prev_brightness;
#endif

static int usb_ep0_req_send(const void *data, unsigned int size)
{
	static SceUdcdDeviceRequest req;

	req = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[0],
		.data = (void *)data,
		.attributes = 0,
		.size = size,
		.isControlRequest = 0,
		.onComplete = NULL,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	ksceKernelCpuDcacheAndL2WritebackRange(data, size);

	return ksceUdcdReqSend(&req);
}

static void usb_ep0_req_recv_on_complete(SceUdcdDeviceRequest *req);

static int usb_ep0_enqueue_recv_for_req(const SceUdcdEP0DeviceRequest *ep0_req)
{
	static SceUdcdDeviceRequest req;

	pending_recv.ep0_req = *ep0_req;

	req = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[0],
		.data = (void *)pending_recv.buffer,
		.attributes = 0,
		.size = pending_recv.ep0_req.wLength,
		.isControlRequest = 0,
		.onComplete = &usb_ep0_req_recv_on_complete,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	ksceKernelCpuDcacheAndL2WritebackInvalidateRange(pending_recv.buffer,
		pending_recv.ep0_req.wLength);

	return ksceUdcdReqRecv(&req);
}

static int uvc_frame_req_init(void)
{
	uvc_frame_req_evflag = ksceKernelCreateEventFlag("uvc_frame_req_evflag", 0, 0, NULL);
	if (uvc_frame_req_evflag < 0) {
		return uvc_frame_req_evflag;
	}

	return 0;
}

static int uvc_frame_req_fini(void)
{
	int ret;

	ret = ksceKernelDeleteEventFlag(uvc_frame_req_evflag);
	if (ret < 0)
		return ret;

	return 0;
}

static void uvc_frame_req_submit_phycont_on_complete(SceUdcdDeviceRequest *req)
{
	ksceKernelSetEventFlag(uvc_frame_req_evflag, 1);
}

static int uvc_frame_req_submit_phycont(const void *data, unsigned int size)
{
	static SceUdcdDeviceRequest req;
	int ret;

	req = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[1],
		.data = (void *)data,
		.attributes = SCE_UDCD_DEVICE_REQUEST_ATTR_PHYCONT,
		.size = size,
		.isControlRequest = 0,
		.onComplete = uvc_frame_req_submit_phycont_on_complete,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	ret = ksceUdcdReqSend(&req);
	if (ret < 0)
		return ret;

	ret = ksceKernelWaitEventFlagCB(uvc_frame_req_evflag, 1, SCE_EVENT_WAITOR |
					SCE_EVENT_WAITCLEAR_PAT, NULL, NULL);

	return ret;
}

static void uvc_handle_video_streaming_req_recv(const SceUdcdEP0DeviceRequest *req)
{
	struct uvc_streaming_control *streaming_control =
		(struct uvc_streaming_control *)pending_recv.buffer;

	switch (req->wValue >> 8) {
	case UVC_VS_PROBE_CONTROL:
		switch (req->bRequest) {
		case UVC_SET_CUR:
			uvc_probe_control_setting.bFormatIndex = streaming_control->bFormatIndex;
			uvc_probe_control_setting.bFrameIndex = streaming_control->bFrameIndex;
			uvc_probe_control_setting.dwFrameInterval = streaming_control->dwFrameInterval;
			LOG("Probe SET_CUR, bFormatIndex: %d, bmFramingInfo: %x\n",
			    uvc_probe_control_setting.bFormatIndex,
			    uvc_probe_control_setting.bmFramingInfo);
			break;
		}
		break;
	case UVC_VS_COMMIT_CONTROL:
		switch (req->bRequest) {
		case UVC_SET_CUR:
			uvc_probe_control_setting.bFormatIndex = streaming_control->bFormatIndex;
			uvc_probe_control_setting.bFrameIndex = streaming_control->bFrameIndex;
			uvc_probe_control_setting.dwFrameInterval = streaming_control->dwFrameInterval;
			LOG("Commit SET_CUR, bFormatIndex: %d, bmFramingInfo: %x\n",
			    uvc_probe_control_setting.bFormatIndex,
			    uvc_probe_control_setting.bmFramingInfo);

			stream = 1;
			ksceKernelSetEventFlag(uvc_event_flag_id, 1);
			break;
		}
		break;
	}
}

void usb_ep0_req_recv_on_complete(SceUdcdDeviceRequest *req)
{
	switch (pending_recv.ep0_req.wIndex & 0xFF) {
	case STREAM_INTERFACE:
		uvc_handle_video_streaming_req_recv(&pending_recv.ep0_req);
		break;
	}
}

static void uvc_handle_interface_ctrl_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_interface_ctrl_req\n");
}

static void uvc_handle_input_terminal_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_input_terminal_req %x, %x\n", req->wValue, req->bRequest);
}

static void uvc_handle_output_terminal_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_output_terminal_req\n");
}

static void uvc_handle_video_streaming_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_video_streaming_req %x, %x\n", req->wValue, req->bRequest);

	switch (req->wValue >> 8) {
	case UVC_VS_PROBE_CONTROL:
		switch (req->bRequest) {
		case UVC_GET_INFO:
			break;
		case UVC_GET_LEN:
			break;
		case UVC_GET_MIN:
		case UVC_GET_MAX:
		case UVC_GET_DEF:
			LOG("Probe GET_DEF, bFormatIndex: %d, bmFramingInfo: %x\n",
			    uvc_probe_control_setting_default.bFormatIndex,
			    uvc_probe_control_setting_default.bmFramingInfo);
			usb_ep0_req_send(&uvc_probe_control_setting_default,
					 sizeof(uvc_probe_control_setting_default));
			break;
		case UVC_GET_CUR:
			LOG("Probe GET_CUR, bFormatIndex: %d, bmFramingInfo: %x\n",
			    uvc_probe_control_setting.bFormatIndex,
			    uvc_probe_control_setting.bmFramingInfo);
			usb_ep0_req_send(&uvc_probe_control_setting,
					 sizeof(uvc_probe_control_setting));
			break;
		case UVC_SET_CUR:
			usb_ep0_enqueue_recv_for_req(req);
			break;
		}
		break;
	case UVC_VS_COMMIT_CONTROL:
		switch (req->bRequest) {
		case UVC_GET_INFO:
			break;
		case UVC_GET_LEN:
			break;
		case UVC_GET_CUR:
			usb_ep0_req_send(&uvc_probe_control_setting,
					 sizeof(uvc_probe_control_setting));
			break;
		case UVC_SET_CUR:
			usb_ep0_enqueue_recv_for_req(req);
			break;
		}
		break;
	}
}

static void uvc_handle_video_abort(void)
{
	LOG("uvc_handle_video_abort\n");

	if (stream) {
		stream = 0;

		ksceUdcdClearFIFO(&endpoints[1]);
		ksceUdcdReqCancelAll(&endpoints[1]);
	}
}

static void uvc_handle_set_interface(const SceUdcdEP0DeviceRequest *req)
{
	LOG("uvc_handle_set_interface %x %x\n", req->wIndex, req->wValue);

	/* MAC OS sends Set Interface Alternate Setting 0 command after
	 * stopping to stream. This application needs to stop streaming. */
	if ((req->wIndex == STREAM_INTERFACE) && (req->wValue == 0))
		uvc_handle_video_abort();
}

static void uvc_handle_clear_feature(const SceUdcdEP0DeviceRequest *req)
{
	LOG("uvc_handle_clear_feature\n");

	/* Windows OS sends Clear Feature Request after it stops streaming,
	 * however MAC OS sends clear feature request right after it sends a
	 * Commit -> SET_CUR request. Hence, stop streaming only of streaming
	 * has started. */
	switch (req->wValue) {
	case USB_FEATURE_ENDPOINT_HALT:
		if ((req->wIndex & USB_ENDPOINT_ADDRESS_MASK) ==
		    endpoints[1].endpointNumber) {
			uvc_handle_video_abort();
		}
		break;
	}
}

static int uvc_udcd_process_request(int recipient, int arg, SceUdcdEP0DeviceRequest *req, void *user_data)
{
	LOG("usb_driver_process_request(recipient: %x, arg: %x)\n", recipient, arg);
	LOG("  request: %x type: %x wValue: %x wIndex: %x wLength: %x\n",
		req->bRequest, req->bmRequestType, req->wValue, req->wIndex, req->wLength);

	if (arg < 0)
		return -1;

	switch (req->bmRequestType) {
	case USB_CTRLTYPE_DIR_DEVICE2HOST |
	     USB_CTRLTYPE_TYPE_CLASS |
	     USB_CTRLTYPE_REC_INTERFACE: /* 0xA1 */
	case USB_CTRLTYPE_DIR_HOST2DEVICE |
	     USB_CTRLTYPE_TYPE_CLASS |
	     USB_CTRLTYPE_REC_INTERFACE: /* 0x21 */
		switch (req->wIndex & 0xFF) {
		case CONTROL_INTERFACE:
			switch (req->wIndex >> 8) {
			case INTERFACE_CTRL_ID:
				uvc_handle_interface_ctrl_req(req);
				break;
			case INPUT_TERMINAL_ID:
				uvc_handle_input_terminal_req(req);
				break;
			case OUTPUT_TERMINAL_ID:
				uvc_handle_output_terminal_req(req);
				break;
			}
			break;
		case STREAM_INTERFACE:
			uvc_handle_video_streaming_req(req);
			break;
		}
		break;
	case USB_CTRLTYPE_DIR_HOST2DEVICE |
	     USB_CTRLTYPE_TYPE_STANDARD |
	     USB_CTRLTYPE_REC_INTERFACE: /* 0x01 */
		switch (req->bRequest) {
		case USB_REQ_SET_INTERFACE:
			uvc_handle_set_interface(req);
			break;
		}
		break;
	case USB_CTRLTYPE_DIR_HOST2DEVICE |
	     USB_CTRLTYPE_TYPE_STANDARD |
	     USB_CTRLTYPE_REC_ENDPOINT: /* 0x02 */
		switch (req->bRequest) {
		case USB_REQ_CLEAR_FEATURE:
			uvc_handle_clear_feature(req);
			break;
		}
		break;
	case USB_CTRLTYPE_DIR_DEVICE2HOST |
	     USB_CTRLTYPE_TYPE_STANDARD |
	     USB_CTRLTYPE_REC_DEVICE: /* 0x80 */
		switch (req->wValue >> 8) {
		case 0x0A: /* USB_DT_DEBUG */
			break;
		}
		break;
	default:
		LOG("Unknown bmRequestType: 0x%02X\n", req->bmRequestType);
	}

	return 0;
}

static int uvc_udcd_change_setting(int interfaceNumber, int alternateSetting, int bus)
{
	LOG("uvc_udcd_change %d %d\n", interfaceNumber, alternateSetting);

	return 0;
}

static int uvc_udcd_attach(int usb_version, void *user_data)
{
	LOG("uvc_udcd_attach %d\n", usb_version);

	ksceUdcdClearFIFO(&endpoints[1]);

#if defined(DISPLAY_OFF_OLED)
	prev_brightness = ksceOledGetBrightness();
	ksceOledDisplayOff();
#elif defined(DISPLAY_OFF_LCD)
	prev_brightness = ksceLcdGetBrightness();
	ksceLcdDisplayOff();
#endif

	return 0;
}

static void uvc_udcd_detach(void *user_data)
{
	LOG("uvc_udcd_detach\n");

	uvc_handle_video_abort();

#if defined(DISPLAY_OFF_OLED)
	ksceOledDisplayOn();
	ksceOledSetBrightness(prev_brightness);
#elif defined(DISPLAY_OFF_LCD)
	ksceLcdDisplayOn();
	ksceLcdSetBrightness(prev_brightness);
#endif
}

static void uvc_udcd_configure(int usb_version, int desc_count, SceUdcdInterfaceSettings *settings, void *user_data)
{
	LOG("uvc_udcd_configure %d %d %p %d\n", usb_version, desc_count, settings, settings->numDescriptors);
}

static int uvc_driver_start(int size, void *p, void *user_data)
{
	LOG("uvc_driver_start\n");

	return 0;
}

static int uvc_driver_stop(int size, void *p, void *user_data)
{
	LOG("uvc_driver_stop\n");

	return 0;
}

static SceUdcdDriver uvc_udcd_driver = {
	.driverName			= UVC_DRIVER_NAME,
	.numEndpoints			= 2,
	.endpoints			= endpoints,
	.interface			= &interface,
	.descriptor_hi			= &devdesc_hi,
	.configuration_hi		= &config_hi,
	.descriptor			= &devdesc_full,
	.configuration			= &config_full,
	.stringDescriptors		= NULL,
	.stringDescriptorProduct	= &string_descriptor_product,
	.stringDescriptorSerial		= &string_descriptor_serial,
	.processRequest			= &uvc_udcd_process_request,
	.changeSetting			= &uvc_udcd_change_setting,
	.attach				= &uvc_udcd_attach,
	.detach				= &uvc_udcd_detach,
	.configure			= &uvc_udcd_configure,
	.start				= &uvc_driver_start,
	.stop				= &uvc_driver_stop,
	.user_data			= NULL
};

static unsigned int uvc_frame_transfer(struct uvc_frame *frame,
				       unsigned int frame_size,
				       int fid, int eof)
{
	int ret;

	frame->header[0] = UVC_PAYLOAD_HEADER_SIZE;
	frame->header[1] = UVC_STREAM_EOH;

	if (fid)
		frame->header[1] |= UVC_STREAM_FID;
	if (eof)
		frame->header[1] |= UVC_STREAM_EOF;

	ret = uvc_frame_req_submit_phycont(frame, frame_size);
	if (ret < 0) {
		LOG("Error sending frame: 0x%08X\n", ret);
		return ret;
	}

	return 0;
}

int uvc_start(void);
int uvc_stop(void);

static inline unsigned int display_to_iftu_pixelformat(unsigned int fmt)
{
	switch (fmt) {
	case SCE_DISPLAY_PIXELFORMAT_A8B8G8R8:
	default:
		return SCE_IFTU_PIXELFORMAT_BGRX8888;
	case 0x50000000:
		return SCE_IFTU_PIXELFORMAT_BGRA5551;
	}
}

static inline unsigned int display_pixelformat_bpp(unsigned int fmt)
{
	switch (fmt) {
	case SCE_DISPLAY_PIXELFORMAT_A8B8G8R8:
	default:
		return 4;
	case 0x50000000:
		return 2;
	}
}

static int frame_convert_to_nv12(int fid, const SceDisplayFrameBufInfo *fb_info,
					int dst_width, int dst_height)
{
	uintptr_t dst_paddr;
	uintptr_t src_paddr = fb_info->paddr;
	unsigned int src_width = fb_info->framebuf.width;
	unsigned int src_width_aligned = ALIGN(src_width, 16);
	unsigned int src_pitch = fb_info->framebuf.pitch;
	unsigned int src_height = fb_info->framebuf.height;
	unsigned int src_pixelfmt = fb_info->framebuf.pixelformat;
	unsigned char *uvc_frame_data = uvc_frame_buffer_addr->data;

	static SceIftuCscParams RGB_to_YCbCr_JPEG_csc_params = {
		0, 0x202, 0x3FF,
		0, 0x3FF,     0,
		{
			{ 0x99, 0x12C,  0x3A},
			{0xFAA, 0xF57, 0x100},
			{0x100, 0xF2A, 0xFD7}
		}
	};

	ksceKernelGetPaddr(uvc_frame_data, &dst_paddr);

	SceIftuConvParams params;
	memset(&params, 0, sizeof(params));
	params.size = sizeof(params);
	params.unk04 = 1;
	params.csc_params1 = &RGB_to_YCbCr_JPEG_csc_params;
	params.csc_params2 = NULL;
	params.csc_control = 1;
	params.unk14 = 0;
	params.unk18 = 0;
	params.unk1C = 0;
	params.alpha = 0xFF;
	params.unk24 = 0;

	SceIftuPlaneState src;
	memset(&src, 0, sizeof(src));
	src.fb.pixelformat = display_to_iftu_pixelformat(src_pixelfmt);
	src.fb.width = src_width_aligned;
	src.fb.height = src_height;
	src.fb.leftover_stride = (src_pitch - src_width_aligned) *
				 display_pixelformat_bpp(src_pixelfmt);
	src.fb.leftover_align = 0;
	src.fb.paddr0 = src_paddr;
	src.unk20 = 0;
	src.src_x = 0;
	src.src_y = 0;
	src.src_w = (src_width * 0x10000) / dst_width;
	src.src_h = (src_height * 0x10000) / dst_height;
	src.dst_x = 0;
	src.dst_y = 0;
	src.dst_w = 0;
	src.dst_h = 0;
	src.vtop_padding = 0;
	src.vbot_padding = 0;
	src.hleft_padding = 0;
	src.hright_padding = 0;

	SceIftuFrameBuf dst;
	memset(&dst, 0, sizeof(dst));
	dst.pixelformat = SCE_IFTU_PIXELFORMAT_NV12;
	dst.width = dst_width;
	dst.height = dst_height;
	dst.leftover_stride = 0;
	dst.leftover_align = 0;
	dst.paddr0 = dst_paddr;
	dst.paddr1 = dst_paddr + dst_width * dst_height;

	return ksceIftuCsc(&dst, &src, &params);
}

static int convert_and_send_frame_nv12(int fid, const SceDisplayFrameBufInfo *fb_info,
					int dst_width, int dst_height)
{
	int ret;
	uint64_t time1, time2, time3;
	UNUSED(time1);
	UNUSED(time2);
	UNUSED(time3);

	time1 = ksceKernelGetSystemTimeWide();

	ret = frame_convert_to_nv12(fid, fb_info, dst_width, dst_height);
	if (ret < 0)
		return ret;

	time2 = ksceKernelGetSystemTimeWide();

	ret = uvc_frame_transfer(uvc_frame_buffer_addr,
				 UVC_PAYLOAD_SIZE(VIDEO_FRAME_SIZE_NV12(dst_width, dst_height)),
				 fid, 1);
	if (ret < 0)
		return ret;

	time3 = ksceKernelGetSystemTimeWide();
	LOG("NV12 CSC: %lldus xfer: %lldus\n", time2 - time1, time3 - time2);

	return 0;
}

static int send_frame(void)
{
	static int fid = 0;

	int ret;
	SceDisplayFrameBufInfo fb_info;
	int head = ksceDisplayGetPrimaryHead();

	memset(&fb_info, 0, sizeof(fb_info));
	fb_info.size = sizeof(fb_info);
	ret = ksceDisplayGetProcFrameBufInternal(-1, head, 0, &fb_info);
	if (ret < 0 || fb_info.paddr == 0)
		ret = ksceDisplayGetProcFrameBufInternal(-1, head, 1, &fb_info);
	if (ret < 0)
		return ret;

	switch (uvc_probe_control_setting.bFormatIndex) {
	case FORMAT_INDEX_UNCOMPRESSED_NV12: {
		const struct UVC_FRAME_UNCOMPRESSED(2) *frames =
			video_streaming_descriptors.frames_uncompressed_nv12;
		int cur_frame_index = uvc_probe_control_setting.bFrameIndex;
		int dst_width = frames[cur_frame_index - 1].wWidth;
		int dst_height = frames[cur_frame_index - 1].wHeight;

		static int last_frame_index = 0;
		if (uvc_frame_buffer_uid < 0 || cur_frame_index != last_frame_index) {
			uvc_frame_term();
			ret = uvc_frame_init(VIDEO_FRAME_SIZE_NV12(dst_width, dst_height));
			if (ret < 0) {
				LOG("Error allocating the UVC frame (0x%08X)\n", ret);
				return ret;
			} else {
				last_frame_index = cur_frame_index;
			}
		}

		ret = convert_and_send_frame_nv12(fid, &fb_info, dst_width, dst_height);
		if (ret < 0) {
			LOG("Error sending NV12 frame: 0x%08X\n", ret);
			return ret;
		}

		break;
	}
	}

	if (ret < 0) {
		stream = 0;
		return ret;
	}

	fid ^= 1;

	return 0;
}

static int display_vblank_cb_func(int notifyId, int notifyCount, int notifyArg, void *common)
{
	static unsigned int frames = 0;
	unsigned int elapsed;

	/*LOG("VBlank: %d, %d, %d, %p\n", notifyId, notifyCount, notifyArg, common);*/

	if (!stream)
		return 0;

	/*
	 * VBlanks occur at ~60FPS.
	 */
	frames += notifyCount;
	elapsed = FPS_TO_INTERVAL(60 / frames);

	if (elapsed >= uvc_probe_control_setting.dwFrameInterval) {
		ksceKernelSetEventFlag(uvc_event_flag_id, 1);
		frames = 0;
	}

	return 0;
}

static int uvc_thread(SceSize args, void *argp)
{
	SceUID display_vblank_cb_uid;
#if 0
	/*
	 * Wait until the MTP driver starts to takeover.
	 */
	SceUdcdWaitParam wait_param;
	memset(&wait_param, 0, sizeof(wait_param));
	wait_param.status = SCE_UDCD_STATUS_ACTIVATED;
	wait_param.driverName = "USB_MTP_Driver";
	ksceUdcdWaitState(&wait_param, 0);
	ksceKernelDelayThread(250 * 1000);
#endif

	stream = 0;
	uvc_start();

	display_vblank_cb_uid = ksceKernelCreateCallback("uvc_display_vblank", 0,
							 display_vblank_cb_func, NULL);

	ksceDisplayRegisterVblankStartCallback(display_vblank_cb_uid);

	while (uvc_thread_run) {
		unsigned int out_bits;

		int ret = ksceKernelWaitEventFlagCB(uvc_event_flag_id, 1,
			SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT,
			&out_bits, (SceUInt32[]){1000000});

		if (ret == 0 && stream)
			send_frame();
		else if (ret == 0x80028005) /* SCE_KERNEL_ERROR_WAIT_TIMEOUT */
			uvc_frame_term();
	}

	ksceDisplayUnregisterVblankStartCallback(display_vblank_cb_uid);
	ksceKernelDeleteCallback(display_vblank_cb_uid);

	uvc_stop();

	return 0;
}

static int uvc_frame_init(unsigned int size)
{
	int ret;

	const int use_cdram = 0;
	SceKernelAllocMemBlockKernelOpt opt;
	SceKernelMemBlockType type;
	SceKernelAllocMemBlockKernelOpt *optp;

	if (use_cdram) {
		type = 0x40408006;
		size = ALIGN(size, 256 * 1024);
		optp = NULL;
	} else {
		type = 0x10208006;
		size = ALIGN(size, 4 * 1024);
		memset(&opt, 0, sizeof(opt));
		opt.size = sizeof(opt);
		opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_PHYCONT |
			   SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
		opt.alignment = 4 * 1024;
		optp = &opt;
	}

	uvc_frame_buffer_uid = ksceKernelAllocMemBlock("uvc_frame_buffer", type, size, optp);
	if (uvc_frame_buffer_uid < 0) {
		LOG("Error allocating CSC dest memory: 0x%08X\n", uvc_frame_buffer_uid);
		return uvc_frame_buffer_uid;
	}

	ret = ksceKernelGetMemBlockBase(uvc_frame_buffer_uid, (void **)&uvc_frame_buffer_addr);
	if (ret < 0) {
		LOG("Error getting CSC desr memory addr: 0x%08X\n", ret);
		ksceKernelFreeMemBlock(uvc_frame_buffer_uid);
		uvc_frame_buffer_uid = -1;
		return ret;
	}

	return 0;
}

static int uvc_frame_term()
{
	if (uvc_frame_buffer_uid >= 0) {
		ksceKernelFreeMemBlock(uvc_frame_buffer_uid);
		uvc_frame_buffer_uid = -1;
	}

	return 0;
}

int uvc_start(void)
{
	int ret;

	/*
	 * Wait until there's a framebuffer set.
	 */
	ksceDisplayWaitSetFrameBufCB();

#ifndef DEBUG
	/*
	 * Wait until LiveArea is more or less ready.
	 */
	ksceKernelDelayThreadCB(5 * 1000 * 1000);
#endif

	ret = ksceUdcdDeactivate();
	if (ret < 0 && ret != SCE_UDCD_ERROR_INVALID_ARGUMENT) {
		LOG("Error deactivating UDCD (0x%08X)\n", ret);
		return ret;
	}

	ksceUdcdStop("USB_MTP_Driver", 0, NULL);
	ksceUdcdStop("USBPSPCommunicationDriver", 0, NULL);
	ksceUdcdStop("USBSerDriver", 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);

	ret = ksceUdcdStart("USBDeviceControllerDriver", 0, NULL);
	if (ret < 0) {
		LOG("Error starting the USBDeviceControllerDriver driver (0x%08X)\n", ret);
		return ret;
	}

	ret = ksceUdcdStart(UVC_DRIVER_NAME, 0, NULL);
	if (ret < 0) {
		LOG("Error starting the " UVC_DRIVER_NAME " driver (0x%08X)\n", ret);
		goto err_start_uvc_driver;
	}

	ret = ksceUdcdActivate(UVC_USB_PID);
	if (ret < 0) {
		LOG("Error activating the " UVC_DRIVER_NAME " driver (0x%08X)\n", ret);
		goto err_activate;
	}

	ret = uvc_frame_req_init();
	if (ret < 0) {
		LOG("Error allocating USB request (0x%08X)\n", ret);
		goto err_alloc_uvc_frame_req;
	}

	/*
	 * Set the current streaming settings to the default ones.
	 */
	memcpy(&uvc_probe_control_setting, &uvc_probe_control_setting_default,
	       sizeof(uvc_probe_control_setting));

	return 0;

err_alloc_uvc_frame_req:
	ksceUdcdDeactivate();
err_activate:
	ksceUdcdStop(UVC_DRIVER_NAME, 0, NULL);
err_start_uvc_driver:
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	return ret;
}

int uvc_stop(void)
{
	ksceUdcdDeactivate();
	ksceUdcdStop(UVC_DRIVER_NAME, 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdStart("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdStart("USB_MTP_Driver", 0, NULL);
	ksceUdcdActivate(0x4E4);

	uvc_frame_term();

	return 0;
}

static SceUID SceUdcd_sub_01E1128C_hook_uid = -1;
static tai_hook_ref_t SceUdcd_sub_01E1128C_ref;

static int SceUdcd_sub_01E1128C_hook_func(const SceUdcdConfigDescriptor *config_descriptor, void *desc_data)
{
	int ret;
	SceUdcdConfigDescriptor *dst = desc_data;

	ret = TAI_CONTINUE(int, SceUdcd_sub_01E1128C_ref, config_descriptor, desc_data);

	/*
	 * SceUdcd doesn't use the extra and extraLength members of the
	 * SceUdcdConfigDescriptor struct, so we have to manually patch
	 * it to add custom descriptors.
	 */
	if (dst->wTotalLength == config_descriptor->wTotalLength) {
		memmove(desc_data + USB_DT_CONFIG_SIZE + sizeof(interface_association_descriptor),
			desc_data + USB_DT_CONFIG_SIZE,
			config_descriptor->wTotalLength - USB_DT_CONFIG_SIZE);

		memcpy(desc_data + USB_DT_CONFIG_SIZE, interface_association_descriptor,
		       sizeof(interface_association_descriptor));

		dst->wTotalLength += sizeof(interface_association_descriptor);

		ksceKernelCpuDcacheAndL2WritebackRange(desc_data, dst->wTotalLength);
	}

	return ret;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args)
{
	int ret;
	tai_module_info_t SceUdcd_modinfo;

#ifdef DEBUG
	log_reset();
	framebuffer_map();
	console_init();
#endif

	LOG("udcd_uvc by xerpi\n");

	SceUdcd_modinfo.size = sizeof(SceUdcd_modinfo);
	taiGetModuleInfoForKernel(KERNEL_PID, "SceUdcd", &SceUdcd_modinfo);

	SceUdcd_sub_01E1128C_hook_uid = taiHookFunctionOffsetForKernel(KERNEL_PID,
		&SceUdcd_sub_01E1128C_ref, SceUdcd_modinfo.modid, 0,
		0x01E1128C - 0x01E10000, 1, SceUdcd_sub_01E1128C_hook_func);

	uvc_thread_id = ksceKernelCreateThread("uvc_thread", uvc_thread,
					       0x3C, 0x1000, 0, 0x10000, 0);
	if (uvc_thread_id < 0) {
		LOG("Error creating the UVC thread (0x%08X)\n", uvc_thread_id);
		goto err_return;
	}

	uvc_event_flag_id = ksceKernelCreateEventFlag("uvc_event_flag", 0,
						      0, NULL);
	if (uvc_event_flag_id < 0) {
		LOG("Error creating the UVC event flag (0x%08X)\n", uvc_event_flag_id);
		goto err_destroy_thread;
	}

	ret = ksceUdcdRegister(&uvc_udcd_driver);
	if (ret < 0) {
		LOG("Error registering the UDCD driver (0x%08X)\n", ret);
		goto err_delete_event_flag;
	}

	uvc_thread_run = 1;

	ret = ksceKernelStartThread(uvc_thread_id, 0, NULL);
	if (ret < 0) {
		LOG("Error starting the UVC thread (0x%08X)\n", ret);
		goto err_unregister;
	}

	return SCE_KERNEL_START_SUCCESS;

err_unregister:
	ksceUdcdUnregister(&uvc_udcd_driver);
err_delete_event_flag:
	ksceKernelDeleteEventFlag(uvc_event_flag_id);
err_destroy_thread:
	ksceKernelDeleteThread(uvc_thread_id);
err_return:
	return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize argc, const void *args)
{
	uvc_thread_run = 0;

	ksceKernelSetEventFlag(uvc_event_flag_id, 1);
	ksceKernelWaitThreadEnd(uvc_thread_id, NULL, NULL);

	ksceKernelDeleteEventFlag(uvc_event_flag_id);
	ksceKernelDeleteThread(uvc_thread_id);

	uvc_frame_req_fini();

	ksceUdcdDeactivate();
	ksceUdcdStop(UVC_DRIVER_NAME, 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdUnregister(&uvc_udcd_driver);

	if (SceUdcd_sub_01E1128C_hook_uid > 0) {
		taiHookReleaseForKernel(SceUdcd_sub_01E1128C_hook_uid,
			SceUdcd_sub_01E1128C_ref);
	}

#ifdef DEBUG
	console_fini();
	framebuffer_unmap();
	log_flush();
#endif

	return SCE_KERNEL_STOP_SUCCESS;
}
