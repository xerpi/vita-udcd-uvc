#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/udcd.h>
#include <taihen.h>
#include "usb_descriptors.h"
#include "uvc.h"
#include "log.h"
#include "draw.h"

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
	0x00,                               /* Framing Information - Ignored for uncompressed format*/
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
	static SceUdcdDeviceRequest req;

	req = (SceUdcdDeviceRequest){
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

	return ksceUdcdReqSend(&req);
}

static int usb_ep0_req_recv(void *data, unsigned int size)
{
	static SceUdcdDeviceRequest req;

	req = (SceUdcdDeviceRequest){
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

	return ksceUdcdReqRecv(&req);
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
	LOG("  uvc_handle_video_streaming_req\n");

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

                        /* TODO: Start streaming */
                        stream = 1;
			break;
		}
		break;
	}
}

static int uvc_udcd_process_request(int recipient, int arg, SceUdcdEP0DeviceRequest *req)
{
	LOG("usb_driver_process_request(recipient: %x, arg: %x)\n", recipient, arg);
	LOG("  request: %x type: %x wValue: %x wIndex: %x wLength: %x\n",
		req->bRequest, req->bmRequestType, req->wValue, req->wIndex, req->wLength);

	if (arg < 0)
		return -1;

	/*uint8_t req_dir = req->bmRequestType & USB_CTRLTYPE_DIR_MASK;
	uint8_t req_type = req->bmRequestType & USB_CTRLTYPE_TYPE_MASK;
	uint8_t req_recipient = req->bmRequestType & USB_CTRLTYPE_REC_MASK;*/

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
	}

#if 0
	if (req_dir == USB_CTRLTYPE_DIR_DEVICE2HOST) {
		LOG("  <-Device to host\n");

		switch (req_type) {
		case USB_CTRLTYPE_TYPE_STANDARD:
			switch (req_recipient) {
			case USB_CTRLTYPE_REC_DEVICE:
				switch (req->bRequest) {
				case USB_REQ_GET_DESCRIPTOR: {
					uint8_t descriptor_type = (req->wValue >> 8) & 0xFF;
					uint8_t descriptor_idx = req->wValue & 0xFF;

					LOG("  USB_REQ_GET_DESCRIPTOR, type: %x, index: %x\n",
						descriptor_type, descriptor_idx);

					switch (descriptor_type) {
					case USB_DT_STRING:
						//send_string_descriptor(descriptor_idx);
						break;
					case 0x0A:	/* Debug descriptor */
						return -1;
					}
					break;
				}

				}
				break;
			case USB_CTRLTYPE_REC_INTERFACE:
				switch (req->bRequest) {
				case USB_REQ_GET_DESCRIPTOR: {
					uint8_t descriptor_type = (req->wValue >> 8) & 0xFF;
					uint8_t descriptor_idx = req->wValue & 0xFF;

					switch (descriptor_type) {
					case HID_DESCRIPTOR_REPORT:
						//send_hid_report_desc();
						break;
					}
				}

				}
				break;
			}
			break;
		case USB_CTRLTYPE_TYPE_CLASS:
			switch (recipient) {
			case USB_CTRLTYPE_REC_INTERFACE:
				switch (req->bRequest) {
				case HID_REQUEST_GET_REPORT: {
					uint8_t report_type = (req->wValue >> 8) & 0xFF;
					uint8_t report_id = req->wValue & 0xFF;

					//if (report_type == 1)/* Input report type */
						//send_hid_report_init(report_id);
					break;
				case UVC_GET_CUR:
					LOG("  UVC_GET_CUR\n");
					break;
				case UVC_GET_DEF:
					LOG("  UVC_GET_DEF\n");
					uint8_t entity_id = req->wIndex >> 8;

					switch (entity_id) {
					case INTERFACE_CTRL_ID:
						LOG("INTERFACE_CTRL_ID\n");
						break;
					}
					break;
				}

				}
				break;
			}
			break;
		}
	} else if (req_dir == USB_CTRLTYPE_DIR_HOST2DEVICE) {
		LOG("->Host to device\n");

		switch (req_type) {
		case USB_CTRLTYPE_TYPE_CLASS:
			switch (req_recipient) {
			case USB_CTRLTYPE_REC_INTERFACE:
				switch (req->bRequest) {
				case HID_REQUEST_SET_IDLE:
					LOG("Set idle!\n");
					break;
				}
				break;
			}
			break;
		}
	}
#endif

	return 0;
}

static int uvc_udcd_change_setting(int interfaceNumber, int alternateSetting)
{
	LOG("uvc_udcd_change %d %d\n", interfaceNumber, alternateSetting);

	return 0;
}

static int uvc_udcd_attach(int usb_version)
{
	LOG("uvc_udcd_attach %d\n", usb_version);

	// ksceUdcdReqCancelAll(&endpoints[1]);
	// ksceUdcdClearFIFO(&endpoints[1]);

	return 0;
}

static void uvc_udcd_detach(void)
{
	LOG("uvc_udcd_detach\n");
}

static void uvc_udcd_configure(int usb_version, int desc_count, SceUdcdInterfaceSettings *settings)
{
	LOG("uvc_udcd_configure %d %d %p %d\n", usb_version, desc_count, settings, settings->numDescriptors);
}

static int uvc_driver_start(int size, void *p)
{
	LOG("uvc_driver_start\n");

	return 0;
}

static int uvc_driver_stop(int size, void *p)
{
	LOG("uvc_driver_stop\n");

	return 0;
}

static SceUdcdDriver uvc_udcd_driver = {
	UVC_DRIVER_NAME,
	4,
	&endpoints[0],
	&interfaces[0],
	&devdesc_hi,
	&config_hi,
	&devdesc_full,
	&config_full,
	&string_descriptors[0],
	&string_descriptors[0],
	&string_descriptors[0],
	&uvc_udcd_process_request,
	&uvc_udcd_change_setting,
	&uvc_udcd_attach,
	&uvc_udcd_detach,
	&uvc_udcd_configure,
	&uvc_driver_start,
	&uvc_driver_stop,
	0,
	0,
	NULL
};

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

void bulk_on_complete(struct SceUdcdDeviceRequest *req)
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

	//LOG("usb_bulk_video_req_send %d\n", n);

	return ksceUdcdReqSend(&reqs[idx]);
}

#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// RGB -> YUV
#define RGB2Y(R, G, B) CLIP(( (  66 * (R) + 129 * (G) +  25 * (B) + 128) >> 8) +  16)
#define RGB2U(R, G, B) CLIP(( ( -38 * (R) -  74 * (G) + 112 * (B) + 128) >> 8) + 128)
#define RGB2V(R, G, B) CLIP(( ( 112 * (R) -  94 * (G) -  18 * (B) + 128) >> 8) + 128)

#define FRAME_WIDTH	640
#define FRAME_HEIGHT	480
#define FRAME_SIZE	(FRAME_WIDTH * FRAME_HEIGHT * 2)
static unsigned char yuv_frame[FRAME_SIZE];

static int usb_thread(SceSize args, void *argp)
{
	stream = 0;
	uvc_start();

	#define PACKET_SIZE 0x200
	static unsigned char packet[PACKET_SIZE] __attribute__((aligned(128)));

	unsigned char uvc_header[12] = {
		0x0C,                               /* Header Length */
		0x8C,                               /* Bit field header field */
		0x00, 0x00, 0x00, 0x00,             /* Presentation time stamp field */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* Source clock reference field */
	};

	for (int i = 0; i < FRAME_SIZE; i += 4) {
		for (int j = 0; j < FRAME_WIDTH; j++) {
			yuv_frame[i + 0] = RGB2Y(255, 0, 0);
			yuv_frame[i + 1] = RGB2U(255, 0, 0);
			yuv_frame[i + 2] = RGB2Y(255, 0, 0);
			yuv_frame[i + 3] = RGB2V(255, 0, 0);
		}
	}

	while (usb_thread_run) {
		if (stream) {
			int num_packets = FRAME_SIZE / (PACKET_SIZE - 12);
			if (FRAME_SIZE % (PACKET_SIZE - 12))
				num_packets++;

			//LOG("num packets: %d\n", num_packets);

			unsigned int offset = 0;

			for (int i = 0; i < num_packets - 1; i++) {
				memcpy(&packet[0], uvc_header, sizeof(uvc_header));
				memcpy(&packet[12], &yuv_frame[offset], PACKET_SIZE - 12);
				ksceKernelCpuDcacheAndL2WritebackRange(packet, sizeof(packet));
				usb_bulk_video_req_send(packet, sizeof(packet));
				offset += PACKET_SIZE - 12;
			}

			//LOG("left: %d\n", FRAME_SIZE - offset);

			memcpy(&packet[0], uvc_header, sizeof(uvc_header));
			memcpy(&packet[12], &yuv_frame[offset], FRAME_SIZE - offset);
			packet[1] |= UVC_STREAM_EOF;
			ksceKernelCpuDcacheAndL2WritebackRange(packet, sizeof(packet));
			usb_bulk_video_req_send(packet, sizeof(packet));

			uvc_header[1] ^= 1;

		}
		ksceKernelDelayThread((1000 * 1000) / 15);
	}

	return 0;
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize argc, const void *args)
{
	int ret;

	log_reset();

	map_framebuffer();
	LOG("udcd_uvc by xerpi\n");

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

	unmap_framebuffer();

	return SCE_KERNEL_STOP_SUCCESS;
}
