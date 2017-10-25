#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/udcd.h>
#include "usb_descriptors.h"
#include "conversion.h"
#include "uvc.h"
#include "log.h"
#include "draw.h"
#include <taihen.h>

#define LOG(s, ...) \
	do { \
		char __buffer[256]; \
		snprintf(__buffer, sizeof(__buffer), s, ##__VA_ARGS__); \
		console_print(__buffer); \
	} while (0)

#define UVC_DRIVER_NAME	"VITAUVC00"
#define UVC_USB_PID	0x1337

#ifdef UVC_1_0_SUPPORT
#define UVC_MAX_PROBE_SETTING		26
#define UVC_MAX_PROBE_SETTING_ALIGNED	32
#else
#define UVC_MAX_PROBE_SETTING		34
#define UVC_MAX_PROBE_SETTING_ALIGNED	48
#endif

/* UVC Probe Control Setting for a USB 2.0 connection. */
static unsigned char uvc_probe_control_setting[UVC_MAX_PROBE_SETTING] = {
	0x00, 0x00,                 /* bmHint : no hit */
	0x01,                       /* Use 1st Video format index */
	0x01,                       /* Use 1st Video frame index */
	0x2A, 0x2C, 0x0A, 0x00,     /* Desired frame interval in the unit of 100ns: 15 fps */
	0x00, 0x00,                 /* Key frame rate in key frame/video frame units: only applicable
				       to video streaming with adjustable compression parameters */
	0x00, 0x00,                 /* PFrame rate in PFrame / key frame units: only applicable to
				       video streaming with adjustable compression parameters */
	0x00, 0x00,                 /* Compression quality control: only applicable to video streaming
				       with adjustable compression parameters */
	0x00, 0x00,                 /* Window size for average bit rate: only applicable to video
				       streaming with adjustable compression parameters */
	0x00, 0x00,                 /* Internal video streaming i/f latency in ms */
	0x00, 0x60, 0x09, 0x00,     /* Max video frame size in bytes */
	0x00, 0x40, 0x00, 0x00,     /* No. of bytes device can rx in single payload = 16 KB */

#ifndef UVC_1_0_SUPPORT
	/* UVC 1.1 Probe Control has additional fields from UVC 1.0 */
	0x00, 0x60, 0xE3, 0x16,             /* Device Clock */
	0x00,                               /* Framing Information - Ignored for uncompressed format */
	0x00,                               /* Preferred payload format version */
	0x00,                               /* Minimum payload format version */
	0x00                                /* Maximum payload format version */
#endif
};

static unsigned char uvc_probe_control_setting_read[UVC_MAX_PROBE_SETTING_ALIGNED];

static SceUID usb_thread_id;
static SceUID usb_event_flag_id;
static int usb_thread_run;
static int stream;

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

static int usb_ep0_req_recv(void *data, unsigned int size)
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

	ksceKernelCpuDcacheAndL2InvalidateRange(data, size);

	return ksceUdcdReqRecv(&reqs[idx]);
}

static void bulk_on_complete(struct SceUdcdDeviceRequest *req)
{
	//LOG("Transmitted: 0x%04X, ret code: 0x%02X\n",
	//	req->transmitted, req->returnCode);
}

static int usb_bulk_video_req_send(const void *data, unsigned int size)
{
	static SceUdcdDeviceRequest reqs[32];
	static int n = 0;
	int idx = n++ % (sizeof(reqs) / sizeof(*reqs));

	reqs[idx] = (SceUdcdDeviceRequest){
		.endpoint = &endpoints[3],
		.data = (void *)data,
		.unk = 0,
		.size = size,
		.isControlRequest = 0,
		.onComplete = bulk_on_complete,
		.transmitted = 0,
		.returnCode = 0,
		.next = NULL,
		.unused = NULL,
		.physicalAddress = NULL
	};

	ksceKernelCpuDcacheAndL2WritebackRange(data, size);

	int ret = ksceUdcdReqSend(&reqs[idx]);

	if (ret != 0)
		LOG("Bulk transfer error 0x%08X\n", ret);

	/*
	 * FIXME: Ugly hack
	 * TODO: Proper request buffering/queuing
	 */
	ksceKernelDelayThread(1000);

	return ret;
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
	/*
	LOG("  uvc_handle_video_streaming_req %x, %x\n", req->wValue, req->bRequest);
	*/

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
			usb_ep0_req_send(uvc_probe_control_setting,
					 sizeof(uvc_probe_control_setting));
			break;
		case UVC_SET_CUR:
			usb_ep0_req_recv(uvc_probe_control_setting_read,
					 sizeof(uvc_probe_control_setting_read));
			uvc_probe_control_setting[2] = uvc_probe_control_setting_read[2];
			uvc_probe_control_setting[3] = uvc_probe_control_setting_read[3];
			uvc_probe_control_setting[4] = uvc_probe_control_setting_read[4];
			uvc_probe_control_setting[5] = uvc_probe_control_setting_read[5];
			uvc_probe_control_setting[6] = uvc_probe_control_setting_read[6];
			uvc_probe_control_setting[7] = uvc_probe_control_setting_read[7];
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
			usb_ep0_req_send(uvc_probe_control_setting,
					 sizeof(uvc_probe_control_setting));
			break;
		case UVC_SET_CUR:
			usb_ep0_req_recv(uvc_probe_control_setting_read,
					 sizeof(uvc_probe_control_setting_read));
			uvc_probe_control_setting[2] = uvc_probe_control_setting_read[2];
			uvc_probe_control_setting[3] = uvc_probe_control_setting_read[3];
			uvc_probe_control_setting[4] = uvc_probe_control_setting_read[4];
			uvc_probe_control_setting[5] = uvc_probe_control_setting_read[5];
			uvc_probe_control_setting[6] = uvc_probe_control_setting_read[6];
			uvc_probe_control_setting[7] = uvc_probe_control_setting_read[7];

                        /* TODO: Start streaming properly */
                        stream = 1;
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

	/*
	LOG("usb_driver_process_request(recipient: %x, arg: %x)\n", recipient, arg);
	LOG("  request: %x type: %x wValue: %x wIndex: %x wLength: %x\n",
		req->bRequest, req->bmRequestType, req->wValue, req->wIndex, req->wLength);
	*/

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

#define PAYLOAD_HEADER_SIZE	12
#define PAYLOAD_TRANSFER_SIZE	0x4000
#define PACKET_SIZE		0x200

static unsigned int uvc_payload_transfer(const unsigned char *data,
					 unsigned int transfer_size,
					 int fid, int eof,
					 unsigned int *written)
{
	static unsigned char packet[PACKET_SIZE] __attribute__((aligned(128)));

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

	/*
	 * The first packet of the transfer includes the payload header.
	 */
	memcpy(&packet[0], payload_header, PAYLOAD_HEADER_SIZE);

	if (transfer_size >= PACKET_SIZE) {
		memcpy(&packet[PAYLOAD_HEADER_SIZE], &data[offset],
		       PACKET_SIZE - PAYLOAD_HEADER_SIZE);
		ret = usb_bulk_video_req_send(packet, PACKET_SIZE);
		if (ret < 0)
			return ret;

		pend_size -= PACKET_SIZE;
		offset += PACKET_SIZE - PAYLOAD_HEADER_SIZE;
	} else {
		memcpy(&packet[PAYLOAD_HEADER_SIZE], &data[offset],
		       transfer_size - PAYLOAD_HEADER_SIZE);
		ret = usb_bulk_video_req_send(packet, transfer_size);
		if (ret < 0)
			return ret;

		pend_size -= transfer_size;
		offset += transfer_size - PAYLOAD_HEADER_SIZE;
	}

	while (pend_size >= PACKET_SIZE) {
		memcpy(packet, &data[offset], PACKET_SIZE);
		ret = usb_bulk_video_req_send(packet, PACKET_SIZE);
		if (ret < 0)
			return ret;

		pend_size -= PACKET_SIZE;
		offset += PACKET_SIZE;
	}

	if (pend_size > 0) {
		memcpy(packet, &data[offset], pend_size);
		ret = usb_bulk_video_req_send(packet, pend_size);
		if (ret < 0)
			return ret;

		offset += pend_size;
	}

	if (written)
		*written = offset;

	return 0;
}

static int uvc_video_frame_transfer(int fid, const unsigned char *data, unsigned int size)
{
	int ret;
	unsigned int written;
	unsigned int offset = 0;
	unsigned int pend_size = size;

	while (pend_size + PAYLOAD_HEADER_SIZE > PAYLOAD_TRANSFER_SIZE) {
		ret = uvc_payload_transfer(&data[offset],
					   PAYLOAD_TRANSFER_SIZE,
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

#define VIDEO_FRAME_WIDTH	640
#define VIDEO_FRAME_HEIGHT	480
#define RGB_VIDEO_FRAME_SIZE	(VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT * 3)
#define YUY2_VIDEO_FRAME_SIZE	(VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT * 2)
static unsigned char rgb_frame[RGB_VIDEO_FRAME_SIZE];
static unsigned char yuy2_frame[YUY2_VIDEO_FRAME_SIZE];

int uvc_start(void);

static void r8g8b8_fill(unsigned char *data, unsigned int width, unsigned int height,
		        unsigned int color)
{
	int i, j;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			data[0 + 3 * (j + i * width)] = (color >> 16) & 0xFF;
			data[1 + 3 * (j + i * width)] = (color >> 8) & 0xFF;
			data[2 + 3 * (j + i * width)] = color & 0xFF;
		}
	}
}

static int usb_thread(SceSize args, void *argp)
{
	int ret;
	int fid = 0;
	unsigned int frames = 0;

	stream = 0;
	uvc_start();

	r8g8b8_fill(rgb_frame, VIDEO_FRAME_WIDTH, VIDEO_FRAME_HEIGHT, 0xFF0000);
	r8g8b8_to_yuy2(rgb_frame, yuy2_frame, VIDEO_FRAME_WIDTH, VIDEO_FRAME_HEIGHT);

	while (usb_thread_run) {
		if (stream) {
			ret = uvc_video_frame_transfer(fid, yuy2_frame, YUY2_VIDEO_FRAME_SIZE);
			if (ret < 0) {
				LOG("Error sending frame: 0x%08X\n", ret);
				stream = 0;
			}

			static unsigned int colors[] = {
				0xFF0000,
				0x00FF00,
				0x0000FF,
				0xFF00FF,
				0xFFFF00,
				0x00FFFF,
				0xFFFFFF,
			};

			r8g8b8_fill(rgb_frame, VIDEO_FRAME_WIDTH, VIDEO_FRAME_HEIGHT,
				colors[((frames++) / 1) % (sizeof(colors) / sizeof(*colors))]);
			r8g8b8_to_yuy2(rgb_frame, yuy2_frame, VIDEO_FRAME_WIDTH, VIDEO_FRAME_HEIGHT);

			fid ^= 1;
		}
		/* ksceKernelDelayThread((1000 * 1000) / 15); */
	}

	return 0;
}

int uvc_start(void)
{
	int ret;
	log_reset();

	LOG("uvc_start\n");

	/*LOG("wTotalLength: 0x%02X\n", uvc_udcd_driver.configuration_hi->configDescriptors[0].wTotalLength);
	LOG("sizeof(interface_association_descriptor): 0x%02X\n", sizeof(interface_association_descriptor));
	LOG("sizeof(video_control_descriptors): 0x%02X\n", sizeof(video_control_descriptors));
	LOG("sizeof(video_streaming_descriptors): 0x%02X\n", sizeof(video_streaming_descriptors));*/

	ret = ksceUdcdDeactivate();
	if (ret < 0 && ret != SCE_UDCD_ERROR_INVALID_ARGUMENT) {
		LOG("Error deactivating UDCD (0x%08X)\n", ret);
		goto err;
	}

	ksceUdcdStop("USB_MTP_Driver", 0, NULL);
	ksceUdcdStop("USBPSPCommunicationDriver", 0, NULL);
	ksceUdcdStop("USBSerDriver", 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);

	ret = ksceUdcdStart("USBDeviceControllerDriver", 0, NULL);
	if (ret < 0) {
		LOG("Error starting the USBDeviceControllerDriver driver (0x%08X)\n", ret);
		goto err;
	}

	ret = ksceUdcdStart(UVC_DRIVER_NAME, 0, NULL);
	if (ret < 0) {
		LOG("Error starting the " UVC_DRIVER_NAME " driver (0x%08X)\n", ret);
		ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
		goto err;
	}

	ret = ksceUdcdActivate(UVC_USB_PID);
	if (ret < 0) {
		LOG("Error activating the " UVC_DRIVER_NAME " driver (0x%08X)\n", ret);
		ksceUdcdStop(UVC_DRIVER_NAME, 0, NULL);
		ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
		goto err;
	}

err:
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

	log_reset();

	map_framebuffer();
	LOG("udcd_uvc by xerpi\n");

	SceUdcd_modinfo.size = sizeof(SceUdcd_modinfo);
	taiGetModuleInfoForKernel(KERNEL_PID, "SceUdcd", &SceUdcd_modinfo);

	SceUdcd_sub_01E1128C_hook_uid = taiHookFunctionOffsetForKernel(KERNEL_PID,
		&SceUdcd_sub_01E1128C_ref, SceUdcd_modinfo.modid, 0,
		0x01E1128C - 0x01E10000, 1, SceUdcd_sub_01E1128C_hook_func);

	usb_thread_id = ksceKernelCreateThread("uvc_usb_thread", usb_thread,
					       0x3C, 0x1000, 0, 0x10000, 0);
	if (usb_thread_id < 0) {
		LOG("Error creating the USB thread (0x%08X)\n", usb_thread_id);
		goto err_return;
	}

	usb_event_flag_id =  ksceKernelCreateEventFlag("uvc_event_flag", 0,
						       0, NULL);
	if (usb_event_flag_id < 0) {
		LOG("Error creating the USB event flag (0x%08X)\n", usb_event_flag_id);
		goto err_destroy_thread;
	}

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

	ksceKernelWaitThreadEnd(usb_thread_id, NULL, NULL);
	ksceKernelDeleteThread(usb_thread_id);

	ksceKernelDeleteEventFlag(usb_event_flag_id);

	ksceUdcdDeactivate();
	ksceUdcdStop(UVC_DRIVER_NAME, 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdUnregister(&uvc_udcd_driver);

	if (SceUdcd_sub_01E1128C_hook_uid > 0) {
		taiHookReleaseForKernel(SceUdcd_sub_01E1128C_hook_uid,
			SceUdcd_sub_01E1128C_ref);
	}

	unmap_framebuffer();

	return SCE_KERNEL_STOP_SUCCESS;
}
