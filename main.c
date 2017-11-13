#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/udcd.h>
#include <psp2kern/display.h>
#include <psp2kern/avcodec/jpegenc.h>
#include "usb_descriptors.h"
#include "conversion.h"
#include "uvc.h"
#include "utils.h"
#include "log.h"
#include "draw.h"
#include <taihen.h>

#ifdef RELEASE
#define LOG(...) (void)0
#else
#define LOG(s, ...) \
	do { \
		char __buffer[256]; \
		snprintf(__buffer, sizeof(__buffer), s, ##__VA_ARGS__); \
		LOG_TO_FILE(__buffer); \
		console_print(__buffer); \
	} while (0)
#endif

#define UVC_DRIVER_NAME	"VITAUVC00"
#define UVC_USB_PID	0x1337

#define MAX_PACKET_SIZE			0x200
#define MAX_PAYLOAD_TRANSFER_SIZE	0x4000
#define MAX_PAYLOAD_TRANSFER_PACKETS	(MAX_PAYLOAD_TRANSFER_SIZE / MAX_PACKET_SIZE)

#define VIDEO_FRAME_WIDTH		960
#define VIDEO_FRAME_HEIGHT		544

#define MAX_VIDEO_FRAME_SIZE		(VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT)

#define PAYLOAD_HEADER_SIZE		12

static struct uvc_streaming_control uvc_probe_control_setting = {
	.bmHint				= 0,
	.bFormatIndex			= 1,
	.bFrameIndex			= 1,
	.dwFrameInterval		= 666666,
	.wKeyFrameRate			= 0,
	.wPFrameRate			= 0,
	.wCompQuality			= 0,
	.wCompWindowSize		= 0,
	.wDelay				= 0,
	.dwMaxVideoFrameSize		= MAX_VIDEO_FRAME_SIZE,
	.dwMaxPayloadTransferSize	= MAX_PAYLOAD_TRANSFER_SIZE,
	.dwClockFrequency		= 384000000,
	.bmFramingInfo			= 0,
	.bPreferedVersion		= 0,
	.bMinVersion			= 0,
	.bMaxVersion			= 0,
};

static struct {
	unsigned char buffer[512];
	SceUdcdEP0DeviceRequest ep0_req;
} pending_recv;

static SceUID usb_thread_id;
static SceUID usb_event_flag_id;
static int usb_thread_run;
static int stream;

static SceUID req_list_memblock;
static void *req_list_addr;
SceUID req_list_evflag;
static unsigned int req_list_size;

static int usb_ep0_req_send(const void *data, unsigned int size)
{
	static SceUdcdDeviceRequest reqs[32];
	static int n = 0;
	int idx = n++ % (sizeof(reqs) / sizeof(*reqs));

	reqs[idx] = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[0],
		.data = (void *)data,
		.unk = 0,
		.size = size,
		.isControlRequest = 0,
		.onComplete = NULL,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	ksceKernelDelayThread(1000);

	ksceKernelCpuDcacheAndL2WritebackRange(data, size);

	return ksceUdcdReqSend(&reqs[idx]);
}

static void usb_ep0_req_recv_on_complete(SceUdcdDeviceRequest *req);

static int usb_ep0_enqueue_recv_for_req(const SceUdcdEP0DeviceRequest *ep0_req)
{
	static SceUdcdDeviceRequest req;

	pending_recv.ep0_req = *ep0_req;

	req = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[0],
		.data = (void *)pending_recv.buffer,
		.unk = 0,
		.size = pending_recv.ep0_req.wLength,
		.isControlRequest = 0,
		.onComplete = &usb_ep0_req_recv_on_complete,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	ksceKernelCpuDcacheAndL2InvalidateRange(pending_recv.buffer,
						pending_recv.ep0_req.wLength);

	return ksceUdcdReqRecv(&req);
}

static int req_list_init(void)
{
	int ret;
	SceKernelAllocMemBlockKernelOpt opt;

	memset(&opt, 0, sizeof(opt));
	opt.size = sizeof(opt);
	opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
	opt.alignment = 4 * 1024;

	req_list_memblock = ksceKernelAllocMemBlock("req_list_memblock",
		SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_RW,
		ALIGN(sizeof(SceUdcdDeviceRequest) * MAX_PAYLOAD_TRANSFER_PACKETS, 4 * 1024),
		&opt);
	if (req_list_memblock < 0)
		return req_list_memblock;

	ret = ksceKernelGetMemBlockBase(req_list_memblock, &req_list_addr);
	if (ret < 0)
		return ret;

	req_list_evflag = ksceKernelCreateEventFlag("req_list_evflag", 0, 0, NULL);
	if (req_list_evflag < 0) {
		ksceKernelFreeMemBlock(req_list_memblock);
		return req_list_evflag;
	}

	req_list_size = 0;

	return 0;
}

static int req_list_fini(void)
{
	int ret;

	ret = ksceKernelFreeMemBlock(req_list_memblock);
	if (ret < 0)
		return ret;

	ret = ksceKernelDeleteEventFlag(req_list_evflag);
	if (ret < 0)
		return ret;

	return 0;
}

static void req_list_reset(void)
{
	req_list_size = 0;
}

static int req_list_enqueue(const void *data, unsigned int size)
{
	SceUdcdDeviceRequest *reqs = (SceUdcdDeviceRequest *)req_list_addr;
	SceUdcdDeviceRequest *new_req = &reqs[req_list_size];

	if (req_list_size >= MAX_PAYLOAD_TRANSFER_PACKETS)
		return -1;

	*new_req = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[3],
		.data = (void *)data,
		.unk = 0,
		.size = size,
		.isControlRequest = 0,
		.onComplete = NULL,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	if (req_list_size > 0)
		reqs[req_list_size - 1].next = new_req;

	req_list_size++;

	return 0;
}

static void req_list_submit_on_complete(SceUdcdDeviceRequest *req)
{
	ksceKernelSetEventFlag(req_list_evflag, 1);
}

static int req_list_submit(void)
{
	int ret;
	unsigned int out_bits;
	SceUdcdDeviceRequest *reqs = (SceUdcdDeviceRequest *)req_list_addr;
	SceUdcdDeviceRequest *cur_req = &reqs[req_list_size - 1];

	if (req_list_size == 0)
		return 0;

	cur_req->onComplete = req_list_submit_on_complete;

	ksceKernelCpuDcacheAndL2WritebackRange(reqs, sizeof(*reqs) * req_list_size);

	ret = ksceUdcdReqSend(reqs);
	if (ret < 0)
		return ret;

	ret = ksceKernelWaitEventFlag(req_list_evflag, 1,
				      SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT,
				      &out_bits, NULL);
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
			break;
		}
		break;
	case UVC_VS_COMMIT_CONTROL:
		switch (req->bRequest) {
		case UVC_SET_CUR:
			uvc_probe_control_setting.bFormatIndex = streaming_control->bFormatIndex;
			uvc_probe_control_setting.bFrameIndex = streaming_control->bFrameIndex;
			uvc_probe_control_setting.dwFrameInterval = streaming_control->dwFrameInterval;

			LOG("Commit, bFormatIndex: %d\n", uvc_probe_control_setting.bFormatIndex);

			stream = 1;
			ksceKernelSetEventFlag(usb_event_flag_id, 1);
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

static void uvc_handle_camera_terminal_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_camera_terminal_req\n");
}

static void uvc_handle_processing_unit_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_processing_unit_req\n");
}

static void uvc_handle_extension_unit_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_extension_unit_req\n");
}

static void uvc_handle_output_terminal_req(const SceUdcdEP0DeviceRequest *req)
{
	LOG("  uvc_handle_output_terminal_req\n");
}

static void uvc_handle_video_streaming_req(const SceUdcdEP0DeviceRequest *req)
{
	/*LOG("  uvc_handle_video_streaming_req %x, %x\n", req->wValue, req->bRequest);*/

	switch (req->wValue >> 8) {
	case UVC_VS_PROBE_CONTROL:
		switch (req->bRequest) {
		case UVC_GET_INFO:
			break;
		case UVC_GET_LEN:
			break;
		case UVC_GET_CUR:
		case UVC_GET_MIN:
		case UVC_GET_MAX:
		case UVC_GET_DEF:
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
	if (stream) {
		stream = 0;

		ksceUdcdReqCancelAll(&endpoints[3]);
		ksceUdcdClearFIFO(&endpoints[3]);
	}
}

static void uvc_handle_clear_feature(const SceUdcdEP0DeviceRequest *req)
{
	switch (req->wValue) {
	case USB_FEATURE_ENDPOINT_HALT:
		if ((req->wIndex & USB_ENDPOINT_ADDRESS_MASK) ==
		    endpoints[3].endpointNumber) {
			uvc_handle_video_abort();
		}
		break;
	}
}

static int uvc_udcd_process_request(int recipient, int arg, SceUdcdEP0DeviceRequest *req, void *user_data)
{
	int ret = 0;

	/*LOG("usb_driver_process_request(recipient: %x, arg: %x)\n", recipient, arg);
	LOG("  request: %x type: %x wValue: %x wIndex: %x wLength: %x\n",
		req->bRequest, req->bmRequestType, req->wValue, req->wIndex, req->wLength);*/

	if (arg < 0)
		ret = -1;

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
			case CAMERA_TERMINAL_ID:
				uvc_handle_camera_terminal_req(req);
				break;
			case PROCESSING_UNIT_ID:
				uvc_handle_processing_unit_req(req);
				break;
			case EXTENSION_UNIT_ID:
				uvc_handle_extension_unit_req(req);
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
			ret = -1;
			break;
		}
		break;
	}

	return ret;
}

static int uvc_udcd_change_setting(int interfaceNumber, int alternateSetting, int bus)
{
	LOG("uvc_udcd_change %d %d\n", interfaceNumber, alternateSetting);

	return 0;
}

static int uvc_udcd_attach(int usb_version, void *user_data)
{
	LOG("uvc_udcd_attach %d\n", usb_version);

	// ksceUdcdReqCancelAll(&endpoints[1]);
	// ksceUdcdClearFIFO(&endpoints[1]);

	return 0;
}

static void uvc_udcd_detach(void *user_data)
{
	LOG("uvc_udcd_detach\n");

	uvc_handle_video_abort();
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
	.numEndpoints			= 4,
	.endpoints			= &endpoints[0],
	.interface			= &interfaces[0],
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

static SceJpegEncoderContext jpegenc_context;
static SceUID jpegenc_context_uid;
static void *jpegenc_context_addr;
static SceUID jpegenc_buffer_uid;
static void *jpegenc_buffer_addr;

static unsigned int uvc_payload_transfer(const unsigned char *data,
					 unsigned int transfer_size,
					 int fid, int eof,
					 unsigned int *written)
{
	static unsigned char packet[MAX_PACKET_SIZE] __attribute__((aligned(512)));

	int ret;
	unsigned int pend_size = transfer_size;
	unsigned int offset = 0;

	unsigned char payload_header[PAYLOAD_HEADER_SIZE] = {
		PAYLOAD_HEADER_SIZE,                /* Header Length */
		UVC_STREAM_EOH,                     /* Bit field header field */
		0x00, 0x00, 0x00, 0x00,             /* Presentation time stamp field */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* Source clock reference field */
	};

	if (fid)
		payload_header[1] |= UVC_STREAM_FID;
	if (eof)
		payload_header[1] |= UVC_STREAM_EOF;

	req_list_reset();

	/*
	 * The first packet of the transfer includes the payload header.
	 */
	memcpy(&packet[0], payload_header, PAYLOAD_HEADER_SIZE);

	if (transfer_size >= MAX_PACKET_SIZE) {
		memcpy(&packet[PAYLOAD_HEADER_SIZE], &data[offset],
		       MAX_PACKET_SIZE - PAYLOAD_HEADER_SIZE);
		ksceKernelCpuDcacheAndL2WritebackRange(packet, MAX_PACKET_SIZE);
		ret = req_list_enqueue(packet, MAX_PACKET_SIZE);
		if (ret < 0)
			return ret;

		pend_size -= MAX_PACKET_SIZE;
		offset += MAX_PACKET_SIZE - PAYLOAD_HEADER_SIZE;
	} else {
		memcpy(&packet[PAYLOAD_HEADER_SIZE], &data[offset],
		       transfer_size - PAYLOAD_HEADER_SIZE);
		ksceKernelCpuDcacheAndL2WritebackRange(packet, transfer_size);
		ret = req_list_enqueue(packet, transfer_size);
		if (ret < 0)
			return ret;

		pend_size -= transfer_size;
		offset += transfer_size - PAYLOAD_HEADER_SIZE;
	}

	while (pend_size >= MAX_PACKET_SIZE) {
		ret = req_list_enqueue(&data[offset], MAX_PACKET_SIZE);
		if (ret < 0)
			return ret;

		pend_size -= MAX_PACKET_SIZE;
		offset += MAX_PACKET_SIZE;
	}

	if (pend_size > 0) {
		ret = req_list_enqueue(&data[offset], pend_size);
		if (ret < 0)
			return ret;

		offset += pend_size;
	}

	if (written)
		*written = offset;

	return req_list_submit();
}

static int uvc_video_frame_transfer(int fid, const unsigned char *data, unsigned int size)
{
	int ret;
	unsigned int written;
	unsigned int offset = 0;
	unsigned int pend_size = size;

	while (pend_size + PAYLOAD_HEADER_SIZE > MAX_PAYLOAD_TRANSFER_SIZE) {
		ret = uvc_payload_transfer(&data[offset],
					   MAX_PAYLOAD_TRANSFER_SIZE,
					   fid, 0, &written);
		if (ret < 0)
			return ret;

		pend_size -= written;
		offset += written;
	}

	if (pend_size > 0) {
		ret = uvc_payload_transfer(&data[offset],
					   pend_size + PAYLOAD_HEADER_SIZE,
					   fid, 1, &written);
		if (ret < 0)
			return ret;

	}

	return 0;
}

int uvc_start(void);
int uvc_stop(void);

static int display_vblank_cb_func(int notifyId, int notifyCount, int notifyArg, void *common)
{
	LOG("Got VBlank!\n");

	return 0;
}

static int usb_thread(SceSize args, void *argp)
{
	int ret;
	SceUID display_vblank_cb_uid;
	int fid = 0;
	SceUID cur_pid = ksceKernelGetProcessId();
	void *rgba_buff_addr = NULL;

	stream = 0;
	uvc_start();

	display_vblank_cb_uid = ksceKernelCreateCallback("uvc_display_vblank", 0,
							 display_vblank_cb_func, NULL);

	ksceDisplayRegisterVblankStartCallback(display_vblank_cb_uid);

	while (usb_thread_run) {
		if (stream) {
			const unsigned int fb_index = 0;
			SceUID userblock;
			SceUID kernelblock;

			SceDisplayFrameBufInfo fb;
			fb.size = sizeof(fb);
			ret = ksceDisplayGetFrameBufInfoForPid(-1, 0, fb_index, &fb);

			if (fb.pid != cur_pid) {
				LOG("Cur PID: 0x%08X, FB PID: 0x%08X\n", cur_pid, fb.pid);

				userblock = ksceKernelFindMemBlockByAddrForPid(fb.pid, fb.framebuf.base, 0);
				if (userblock < 0)
					continue;

				LOG("userblock: 0x%08X\n", userblock);

				SceKernelAllocMemBlockKernelOpt opt;
				opt.size = sizeof(opt);
				opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_MIRROR_BLOCKID | 0x1000000;
				opt.mirror_blockid = userblock;

				kernelblock = ksceKernelAllocMemBlock("mirror_block",
					SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_RW,
					fb.framebuf.pitch * fb.framebuf.height * 4,
					&opt);
				if (kernelblock < 0)
					continue;

				LOG("kernelblock: 0x%08X\n", kernelblock);

				void *base = NULL;
				ret = ksceKernelGetMemBlockBase(kernelblock, &base);
				if (ret < 0) {
					ksceKernelFreeMemBlock(kernelblock);
					continue;
				}

				rgba_buff_addr = base;
			} else {
				rgba_buff_addr = fb.framebuf.base;
			}

			unsigned int pitch = fb.framebuf.pitch;

			if (rgba_buff_addr) {
				uint64_t time1 = ksceKernelGetSystemTimeWide();

				ret = ksceJpegEncoderCsc(jpegenc_context, jpegenc_buffer_addr, rgba_buff_addr,
							 pitch, SCE_JPEGENC_PIXELFORMAT_ARGB8888);
				if (ret < 0)
					LOG("ksceJpegEncoderCsc: 0x%08X\n", ret);

				uint64_t time2 = ksceKernelGetSystemTimeWide();

				ret = ksceJpegEncoderEncode(jpegenc_context, jpegenc_buffer_addr);
				if (ret < 0)
					LOG("ksceJpegEncoderEncode: 0x%08X\n", ret);

				uint64_t time3 = ksceKernelGetSystemTimeWide();

				ret = uvc_video_frame_transfer(fid, (unsigned char *)jpegenc_buffer_addr + 960 * 544 * 2, ret);
				if (ret < 0) {
					LOG("Error sending frame: 0x%08X\n", ret);
					stream = 0;
				}

				uint64_t time4 = ksceKernelGetSystemTimeWide();

				uint64_t delta_csc = time2 - time1;
				uint64_t delta_enc = time3 - time2;
				uint64_t delta_xfer = time4 - time3;

				LOG("CSC: %lldms, JPEG enc: %lldms, transfer: %lldms\n",
				    delta_csc / 1000, delta_enc / 1000,
				    delta_xfer / 1000);
			}

			fid ^= 1;

			if (fb.pid != cur_pid) {
				/*ret = ksceKernelFreeMemBlock(userblock);
				LOG("Free userblock: 0x%08X\n", ret);*/
				ret = ksceKernelFreeMemBlock(kernelblock);
				LOG("Free kernelblock: 0x%08X\n", ret);
			}
		} else {
			unsigned int out_bits;

			ksceKernelWaitEventFlag(usb_event_flag_id, 1,
				      SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT,
				      &out_bits, NULL);
		}
	}

	ksceDisplayUnregisterVblankStartCallback(display_vblank_cb_uid);
	ksceKernelDeleteCallback(display_vblank_cb_uid);

	uvc_stop();

	return 0;
}

static int jpegenc_init(unsigned int width, unsigned int height, unsigned int streambuf_size)
{
	int ret;
	unsigned int context_size;
	unsigned int totalbuf_size;
	unsigned int framebuf_size = width * height * 2;
	SceJpegEncoderPixelFormat pixelformat = SCE_JPEGENC_PIXELFORMAT_YCBCR422 |
						SCE_JPEGENC_PIXELFORMAT_CSC_ARGB_YCBCR;

	framebuf_size = ALIGN(framebuf_size, 256);
	streambuf_size = ALIGN(streambuf_size, 256);
	totalbuf_size = ALIGN(framebuf_size + streambuf_size, 256 * 1024);
	context_size = ALIGN(ksceJpegEncoderGetContextSize(), 4 * 1024);

	jpegenc_context_uid = ksceKernelAllocMemBlock("uvc_jpegenc_context",
		SCE_KERNEL_MEMBLOCK_TYPE_KERNEL_RW, context_size, NULL);
	if (jpegenc_context_uid < 0) {
		LOG("Error allocating JPEG encoder context: 0x%08X\n", jpegenc_context_uid);
		return jpegenc_context_uid;
	}

	ret = ksceKernelGetMemBlockBase(jpegenc_context_uid, &jpegenc_context_addr);
	if (ret < 0) {
		LOG("Error getting JPEG encoder context memory addr: 0x%08X\n", ret);
		goto err_free_context;
	}

	jpegenc_context = jpegenc_context_addr;

	const int use_cdram = 0;
	SceKernelAllocMemBlockKernelOpt opt;
	SceKernelMemBlockType type;
	SceKernelAllocMemBlockKernelOpt *optp;

	if (use_cdram) {
		type = 0x40408006;
		optp = NULL;
	} else {
		type = 0x30808006;
		memset(&opt, 0, sizeof(opt));
		opt.size = sizeof(opt);
		opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_PHYCONT |
			   SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
		opt.alignment = 4 * 1024;
		optp = &opt;
	}

	jpegenc_buffer_uid = ksceKernelAllocMemBlock("uvc_jpegenc_buffer", type, totalbuf_size, optp);
	if (jpegenc_buffer_uid < 0) {
		LOG("Error allocating JPEG encoder memory: 0x%08X\n", jpegenc_buffer_uid);
		ret = jpegenc_buffer_uid;
		goto err_free_context;
	}

	ret = ksceKernelGetMemBlockBase(jpegenc_buffer_uid, &jpegenc_buffer_addr);
	if (ret < 0) {
		LOG("Error getting JPEG encoder memory addr: 0x%08X\n", ret);
		goto err_free_buff;
	}

	ret = ksceJpegEncoderInit(jpegenc_context, width, height, pixelformat,
				  (unsigned char *)jpegenc_buffer_addr + framebuf_size,
				  streambuf_size);
	if (ret < 0) {
		LOG("Error initializing the JPEG encoder: 0x%08X\n", ret);
		goto err_free_buff;
	}

	if (!use_cdram) {
		struct {
			void *outBuffer;
			void *outBufferEnd;
			uint32_t data008;
			uint8_t data00C;
			uint8_t compressionRatio;
			uint8_t data00E;
			uint8_t data00F;
			uint32_t data010;
			uint16_t validRegionWidth;
			uint16_t validRegionHeight;
			uint16_t inWidth;
			uint16_t inHeight;
			uint8_t pixelformat;
			uint8_t headerMode;
			uint8_t unk01E;
			uint8_t unk01F;
			uint32_t unk020;
			uint32_t option;
			uint32_t data028[(0x180 - 0x028) / 4];
		} *context_impl = jpegenc_context;

		context_impl->option = 1;
	}

	ksceJpegEncoderSetCompressionRatio(jpegenc_context, 64);
	ksceJpegEncoderSetHeaderMode(jpegenc_context, SCE_JPEGENC_HEADER_MODE_MJPEG);

	return 0;

err_free_buff:
	ksceKernelFreeMemBlock(jpegenc_buffer_uid);
	jpegenc_buffer_uid = -1;
err_free_context:
	ksceKernelFreeMemBlock(jpegenc_context_uid);
	jpegenc_context_uid = -1;
	return ret;
}

static int jpegenc_term()
{
	ksceJpegEncoderEnd(jpegenc_context);

	if (jpegenc_buffer_uid >= 0) {
		ksceKernelFreeMemBlock(jpegenc_buffer_uid);
		jpegenc_buffer_uid = -1;
	}

	if (jpegenc_context_uid >= 0) {
		ksceKernelFreeMemBlock(jpegenc_context_uid);
		jpegenc_context_uid = -1;
	}

	return 0;
}

int uvc_start(void)
{
	int ret;

#ifdef RELEASE
	ksceKernelDelayThread(5 * 1000 * 1000);
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

	ret = jpegenc_init(VIDEO_FRAME_WIDTH, VIDEO_FRAME_HEIGHT, VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT);
	if (ret < 0) {
		LOG("Error initiating the JPEG encoder (0x%08X)\n", ret);
		goto err_jpegenc_init;
	}

	return 0;

err_jpegenc_init:
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

	jpegenc_term();

	return 0;
}

static SceUID SceUdcd_sub_01E1128C_hook_uid = -1;
static tai_hook_ref_t SceUdcd_sub_01E1128C_ref;
static SceUID SceSysmemForDriver_BC0A1D60_hook_uid = -1;
static tai_hook_ref_t SceSysmemForDriver_BC0A1D60_hook_ref;
static SceUID SceSysmemForDriver_22CBE925_hook_uid = -1;
static tai_hook_ref_t SceSysmemForDriver_22CBE925_hook_ref;

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

int SceSysmemForDriver_BC0A1D60_hook_func(unsigned int perm, void *base, unsigned int size)
{
	if (perm == 0x10)
		perm = 0x01;
	else if (perm == 0x20)
		perm = 0x02;

	return TAI_CONTINUE(int, SceSysmemForDriver_BC0A1D60_hook_ref, perm, base, size);
}

int SceSysmemForDriver_22CBE925_hook_func(unsigned int perm, void *base, unsigned int size)
{
	if (perm == 0x10)
		perm = 0x01;
	else if (perm == 0x20)
		perm = 0x02;

	return TAI_CONTINUE(int, SceSysmemForDriver_22CBE925_hook_ref, perm, base, size);
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args)
{
	int ret;
	tai_module_info_t SceUdcd_modinfo;

	log_reset();

#ifndef RELEASE
	map_framebuffer();
#endif

	LOG("udcd_uvc by xerpi\n");

	SceUdcd_modinfo.size = sizeof(SceUdcd_modinfo);
	taiGetModuleInfoForKernel(KERNEL_PID, "SceUdcd", &SceUdcd_modinfo);

	SceUdcd_sub_01E1128C_hook_uid = taiHookFunctionOffsetForKernel(KERNEL_PID,
		&SceUdcd_sub_01E1128C_ref, SceUdcd_modinfo.modid, 0,
		0x01E1128C - 0x01E10000, 1, SceUdcd_sub_01E1128C_hook_func);

	SceSysmemForDriver_BC0A1D60_hook_uid = taiHookFunctionImportForKernel(KERNEL_PID,
		&SceSysmemForDriver_BC0A1D60_hook_ref,
		"SceAvcodec",
		0x6F25E18A, /* SceSysmemForDriver */
		0xBC0A1D60,
		SceSysmemForDriver_BC0A1D60_hook_func);

	SceSysmemForDriver_22CBE925_hook_uid = taiHookFunctionImportForKernel(KERNEL_PID,
		&SceSysmemForDriver_22CBE925_hook_ref,
		"SceAvcodec",
		0x6F25E18A, /* SceSysmemForDriver */
		0x22CBE925,
		SceSysmemForDriver_22CBE925_hook_func);

	usb_thread_id = ksceKernelCreateThread("uvc_usb_thread", usb_thread,
					       0x3C, 0x1000, 0, 0x10000, 0);
	if (usb_thread_id < 0) {
		LOG("Error creating the USB thread (0x%08X)\n", usb_thread_id);
		goto err_return;
	}

	usb_event_flag_id = ksceKernelCreateEventFlag("uvc_event_flag", 0,
						      0, NULL);
	if (usb_event_flag_id < 0) {
		LOG("Error creating the USB event flag (0x%08X)\n", usb_event_flag_id);
		goto err_destroy_thread;
	}

	req_list_init();

	ret = ksceUdcdRegister(&uvc_udcd_driver);
	if (ret < 0) {
		LOG("Error registering the UDCD driver (0x%08X)\n", ret);
		goto err_delete_event_flag;
	}

	usb_thread_run = 1;

	ret = ksceKernelStartThread(usb_thread_id, 0, NULL);
	if (ret < 0) {
		LOG("Error starting the USB thread (0x%08X)\n", ret);
		goto err_unregister;
	}

	LOG("module_start done successfully!\n");

	return SCE_KERNEL_START_SUCCESS;

err_unregister:
	ksceUdcdUnregister(&uvc_udcd_driver);
err_delete_event_flag:
	ksceKernelDeleteEventFlag(usb_event_flag_id);
err_destroy_thread:
	ksceKernelDeleteThread(usb_thread_id);
err_return:
	return SCE_KERNEL_START_FAILED;
}

int module_stop(SceSize argc, const void *args)
{
	usb_thread_run = 0;

	ksceKernelDeleteEventFlag(usb_event_flag_id);

	ksceKernelWaitThreadEnd(usb_thread_id, NULL, NULL);
	ksceKernelDeleteThread(usb_thread_id);

	req_list_fini();

	ksceUdcdDeactivate();
	ksceUdcdStop(UVC_DRIVER_NAME, 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdUnregister(&uvc_udcd_driver);

	if (SceUdcd_sub_01E1128C_hook_uid > 0) {
		taiHookReleaseForKernel(SceUdcd_sub_01E1128C_hook_uid,
			SceUdcd_sub_01E1128C_ref);
	}

	if (SceSysmemForDriver_BC0A1D60_hook_uid > 0) {
		taiHookReleaseForKernel(SceSysmemForDriver_BC0A1D60_hook_uid,
			SceSysmemForDriver_BC0A1D60_hook_ref);
	}

	if (SceSysmemForDriver_22CBE925_hook_uid > 0) {
		taiHookReleaseForKernel(SceSysmemForDriver_22CBE925_hook_uid,
			SceSysmemForDriver_22CBE925_hook_ref);
	}

	unmap_framebuffer();

	return SCE_KERNEL_STOP_SUCCESS;
}
