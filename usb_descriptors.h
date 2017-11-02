#include "uvc.h"

/*
 * USB definitions
 */

#define USB_TYPE_MASK			(0x03 << 5)
#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_DT_CS_DEVICE		(USB_TYPE_CLASS | USB_DT_DEVICE)
#define USB_DT_CS_CONFIG		(USB_TYPE_CLASS | USB_DT_CONFIG)
#define USB_DT_CS_STRING		(USB_TYPE_CLASS | USB_DT_STRING)
#define USB_DT_CS_INTERFACE		(USB_TYPE_CLASS | USB_DT_INTERFACE)
#define USB_DT_CS_ENDPOINT		(USB_TYPE_CLASS | USB_DT_ENDPOINT)

/*
 * UVC Configurable options
 */

#define CONTROL_INTERFACE 	0
#define STREAM_INTERFACE	1

#define INTERFACE_CTRL_ID	0
#define CAMERA_TERMINAL_ID	1
#define PROCESSING_UNIT_ID	2
#define EXTENSION_UNIT_ID	3
#define OUTPUT_TERMINAL_ID	4

static
unsigned char interface_association_descriptor[] = {
	/* Interface Association Descriptor */
	0x08,			/* Descriptor Size */
	0x0B,			/* Interface Association Descr Type: 11 */
	0x00,			/* I/f number of first VideoControl i/f */
	0x02,			/* Number of Video i/f */
	0x0E,			/* CC_VIDEO : Video i/f class code */
	0x03,			/* SC_VIDEO_INTERFACE_COLLECTION : Subclass code */
	0x00,			/* Protocol : Not used */
	0x00,			/* String desc index for interface */
};

DECLARE_UVC_HEADER_DESCRIPTOR(1);
DECLARE_UVC_EXTENSION_UNIT_DESCRIPTOR(1, 3);

static struct __attribute__((packed)) {
	struct UVC_HEADER_DESCRIPTOR(1) header_descriptor;
	struct uvc_camera_terminal_descriptor input_camera_terminal_descriptor;
	struct uvc_processing_unit_descriptor processing_unit_descriptor;
	struct UVC_EXTENSION_UNIT_DESCRIPTOR(1, 3) extension_unit_descriptor;
	struct uvc_output_terminal_descriptor output_terminal_descriptor;
} video_control_descriptors = {
	.header_descriptor = {
		.bLength			= sizeof(video_control_descriptors.header_descriptor),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VC_HEADER,
		.bcdUVC				= 0x0110,
		.wTotalLength			= sizeof(video_control_descriptors),
		.dwClockFrequency		= 48000000,
		.bInCollection			= 1,
		.baInterfaceNr			= {1},
	},
	.input_camera_terminal_descriptor = {
		.bLength			= sizeof(video_control_descriptors.input_camera_terminal_descriptor),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VC_INPUT_TERMINAL,
		.bTerminalID			= CAMERA_TERMINAL_ID,
		.wTerminalType			= UVC_ITT_CAMERA,
		.bAssocTerminal			= 0,
		.iTerminal			= 0,
		.wObjectiveFocalLengthMin	= 0,
		.wObjectiveFocalLengthMax	= 0,
		.wOcularFocalLength		= 0,
		.bControlSize			= 3,
		.bmControls			= {0, 0, 0},
	},
	.processing_unit_descriptor = {
		.bLength			= sizeof(video_control_descriptors.processing_unit_descriptor),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VC_PROCESSING_UNIT,
		.bUnitID			= PROCESSING_UNIT_ID,
		.bSourceID			= CAMERA_TERMINAL_ID,
		.wMaxMultiplier			= 0x4000,
		.bControlSize			= 2,
		.bmControls			= {0, 0},
		.iProcessing			= 0,
	},
	.extension_unit_descriptor = {
		.bLength			= sizeof(video_control_descriptors.extension_unit_descriptor),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VC_EXTENSION_UNIT,
		.bUnitID			= EXTENSION_UNIT_ID,
		.guidExtensionCode		= {
			0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF,
		},
		.bNumControls			= 0,
		.bNrInPins			= 1,
		.baSourceID			= {EXTENSION_UNIT_ID},
		.bControlSize			= 3,
		.bmControls			= {0, 0, 0},
		.iExtension			= 0,
	},
	.output_terminal_descriptor = {
		.bLength			= sizeof(video_control_descriptors.output_terminal_descriptor),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VC_OUTPUT_TERMINAL,
		.bTerminalID			= OUTPUT_TERMINAL_ID,
		.wTerminalType			= UVC_TT_STREAMING,
		.bAssocTerminal			= 0,
		.bSourceID			= EXTENSION_UNIT_ID,
		.iTerminal			= 0,
	},
};

static
unsigned char video_control_specific_endpoint_descriptors[] = {
	/* Class-specific Interrupt Endpoint Descriptor */
	0x05,                           /* Descriptor size */
	0x25,                           /* Class Specific Endpoint Descriptor Type */
	3,                              /* End point Sub Type */
	0x40,0x00,                      /* Max packet size = 64 bytes */
};

DECLARE_UVC_INPUT_HEADER_DESCRIPTOR(1, 1);
DECLARE_UVC_FRAME_UNCOMPRESSED(1);

static struct __attribute__((packed)) {
	struct UVC_INPUT_HEADER_DESCRIPTOR(1, 1) input_header_descriptor;
	struct uvc_format_uncompressed format_uncompressed;
	struct UVC_FRAME_UNCOMPRESSED(1) frame_uncompressed;
} video_streaming_descriptors = {
	.input_header_descriptor = {
		.bLength			= sizeof(video_streaming_descriptors.input_header_descriptor),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VS_INPUT_HEADER,
		.bNumFormats			= 1,
		.wTotalLength			= sizeof(video_streaming_descriptors),
		.bEndpointAddress		= 0x83,
		.bmInfo				= 0,
		.bTerminalLink			= OUTPUT_TERMINAL_ID,
		.bStillCaptureMethod		= 1,
		.bTriggerSupport		= 0,
		.bTriggerUsage			= 0,
		.bControlSize			= 1,
		.bmaControls			= {{0}, },
	},
	.format_uncompressed = {
		.bLength			= sizeof(video_streaming_descriptors.format_uncompressed),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VS_FORMAT_UNCOMPRESSED,
		.bFormatIndex			= 1,
		.bNumFrameDescriptors		= 1,
		.guidFormat			= {
			0x59, 0x55, 0x59, 0x32,
			0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xAA,
			0x00, 0x38, 0x9B, 0x71,
		},
		.bBitsPerPixel			= 16,
		.bDefaultFrameIndex		= 1,
		.bAspectRatioX			= 8,
		.bAspectRatioY			= 6,
		.bmInterfaceFlags		= 0,
		.bCopyProtect			= 0,
	},
	.frame_uncompressed = {
		.bLength			= sizeof(video_streaming_descriptors.frame_uncompressed),
		.bDescriptorType		= USB_DT_CS_INTERFACE,
		.bDescriptorSubType		= UVC_VS_FRAME_UNCOMPRESSED,
		.bFrameIndex			= 1,
		.bmCapabilities			= 1,
		.wWidth				= 960,
		.wHeight			= 544,
		.dwMinBitRate			= 832000000,
		.dwMaxBitRate			= 832000000,
		.dwMaxVideoFrameBufferSize	= 960 * 544 * 2,
		.dwDefaultFrameInterval		= 666666,
		.bFrameIntervalType		= 1,
		.dwFrameInterval		= {666666},
	},
};

/* Endpoint blocks */
static
struct SceUdcdEndpoint endpoints[4] = {
	{0x00, 0, 0, 0},
	{0x00, 1, 0, 0},
	{0x80, 2, 0, 0},
	{0x80, 3, 0, 0}
};

/* Interfaces */
static
struct SceUdcdInterface interfaces[1] = {
	{-1, 0, 2},
};

/* String descriptors */
static
struct SceUdcdStringDescriptor string_descriptor_product = {
	14,
	USB_DT_STRING,
	{'P', 'S', 'V', 'i', 't', 'a'}
};

static
struct SceUdcdStringDescriptor string_descriptor_serial = {
	18,
	USB_DT_STRING,
	{'V', 'i', 't', 'a', ' ', 'U', 'V', 'C'}
};

/* Hi-Speed device descriptor */
static
struct SceUdcdDeviceDescriptor devdesc_hi = {
	USB_DT_DEVICE_SIZE,
	USB_DT_DEVICE,
	0x200,			/* bcdUSB */
	0xEF,			/* bDeviceClass */
	0x02,			/* bDeviceSubClass */
	0x01,			/* bDeviceProtocol */
	64,			/* bMaxPacketSize0 */
	0,			/* idProduct */
	0,			/* idVendor */
	0x100,			/* bcdDevice */
	0,			/* iManufacturer */
	2,			/* iProduct */
	3,			/* iSerialNumber */
	1			/* bNumConfigurations */
};

/* Hi-Speed endpoint descriptors */
static
struct SceUdcdEndpointDescriptor endpdesc_hi[4] = {
	/* Video Control endpoints */
	{
		USB_DT_ENDPOINT_SIZE,
		USB_DT_ENDPOINT,
		0x01,			/* bEndpointAddress */
		0x02,			/* bmAttributes */
		0x200,			/* wMaxPacketSize */
		0x00			/* bInterval */
	},
	{
		USB_DT_ENDPOINT_SIZE,
		USB_DT_ENDPOINT,
		0x82,			/* bEndpointAddress */
		0x03,			/* bmAttributes */
		0x40,			/* wMaxPacketSize */
		0x01			/* bInterval */
	},
	/* Video Streaming endpoints */
	{
		USB_DT_ENDPOINT_SIZE,
		USB_DT_ENDPOINT,
		0x83,			/* bEndpointAddress */
		0x02,			/* bmAttributes */
		0x200,			/* wMaxPacketSize */
		0x00			/* bInterval */
	},
	{
		0,
	}
};

/* Hi-Speed interface descriptor */
static
struct SceUdcdInterfaceDescriptor interdesc_hi[3] = {
	{	/* Standard Video Control Interface Descriptor */
		USB_DT_INTERFACE_SIZE,
		USB_DT_INTERFACE,
		CONTROL_INTERFACE,		/* bInterfaceNumber */
		0,				/* bAlternateSetting */
		2,				/* bNumEndpoints */
		0x0E,				/* bInterfaceClass */
		0x01,				/* bInterfaceSubClass */
		0x00,				/* bInterfaceProtocol */
		0,				/* iInterface */
		&endpdesc_hi[0],		/* endpoints */
		(void *)&video_control_descriptors,
		sizeof(video_control_descriptors)
	},
	{	/* Standard Video Streaming Interface Descriptor */
		USB_DT_INTERFACE_SIZE,
		USB_DT_INTERFACE,
		STREAM_INTERFACE,		/* bInterfaceNumber */
		0,				/* bAlternateSetting */
		1,				/* bNumEndpoints */
		0x0E,				/* bInterfaceClass */
		0x02,				/* bInterfaceSubClass */
		0x00,				/* bInterfaceProtocol */
		0,				/* iInterface */
		&endpdesc_hi[2],		/* endpoints */
		(void *)&video_streaming_descriptors,
		sizeof(video_streaming_descriptors)
	},
	{
		0
	}
};

/* Hi-Speed settings */
static
struct SceUdcdInterfaceSettings settings_hi[2] = {
	{
		&interdesc_hi[0],
		0,
		1
	},
	{
		&interdesc_hi[1],
		0,
		1
	}
};

/* Hi-Speed configuration descriptor */
static
struct SceUdcdConfigDescriptor confdesc_hi = {
	USB_DT_CONFIG_SIZE,
	USB_DT_CONFIG,
	(USB_DT_CONFIG_SIZE + 2 * USB_DT_INTERFACE_SIZE + 3 * USB_DT_ENDPOINT_SIZE +
		/* sizeof(interface_association_descriptor) + */
		sizeof(video_control_descriptors) +
		sizeof(video_streaming_descriptors)),	/* wTotalLength */
	2,			/* bNumInterfaces */
	1,			/* bConfigurationValue */
	0,			/* iConfiguration */
	0xC0,			/* bmAttributes */
	0,			/* bMaxPower */
	&settings_hi[0],
	interface_association_descriptor,
	sizeof(interface_association_descriptor)
};

/* Hi-Speed configuration */
static
struct SceUdcdConfiguration config_hi = {
	&confdesc_hi,
	&settings_hi[0],
	&interdesc_hi[0],
	&endpdesc_hi[0]
};

/* Full-Speed device descriptor */
static
struct SceUdcdDeviceDescriptor devdesc_full = {
	USB_DT_DEVICE_SIZE,
	USB_DT_DEVICE,
	0x200,			/* bcdUSB (should be 0x110 but the PSVita freezes otherwise) */
	0xEF,			/* bDeviceClass */
	0x02,			/* bDeviceSubClass */
	0x01,			/* bDeviceProtocol */
	0x40,			/* bMaxPacketSize0 */
	0,			/* idProduct */
	0,			/* idVendor */
	0x100,			/* bcdDevice */
	0,			/* iManufacturer */
	2,			/* iProduct */
	3,			/* iSerialNumber */
	1			/* bNumConfigurations */
};

/* Full-Speed endpoint descriptors */
static
struct SceUdcdEndpointDescriptor endpdesc_full[4] = {
	/* Video Control endpoints */
	{
		USB_DT_ENDPOINT_SIZE,
		USB_DT_ENDPOINT,
		0x01,			/* bEndpointAddress */
		0x02,			/* bmAttributes */
		0x40,			/* wMaxPacketSize */
		0x00			/* bInterval */
	},
	{
		USB_DT_ENDPOINT_SIZE,
		USB_DT_ENDPOINT,
		0x82,			/* bEndpointAddress */
		0x03,			/* bmAttributes */
		0x40,			/* wMaxPacketSize */
		0x01			/* bInterval */
	},
	/* Video Streaming endpoints */
	{
		USB_DT_ENDPOINT_SIZE,
		USB_DT_ENDPOINT,
		0x83,			/* bEndpointAddress */
		0x02,			/* bmAttributes */
		0x40,			/* wMaxPacketSize */
		0x00			/* bInterval */
	},
	{
		0,
	}
};

/* Full-Speed interface descriptor */
static
struct SceUdcdInterfaceDescriptor interdesc_full[3] = {
	{	/* Standard Video Control Interface Descriptor */
		USB_DT_INTERFACE_SIZE,
		USB_DT_INTERFACE,
		0,				/* bInterfaceNumber */
		0,				/* bAlternateSetting */
		2,				/* bNumEndpoints */
		14,				/* bInterfaceClass */
		0x01,				/* bInterfaceSubClass */
		0x00,				/* bInterfaceProtocol */
		1,				/* iInterface */
		&endpdesc_full[0],		/* endpoints */
		(void *)&video_control_descriptors,
		sizeof(video_control_descriptors)
	},
	{	/* Standard Video Streaming Interface Descriptor */
		USB_DT_INTERFACE_SIZE,
		USB_DT_INTERFACE,
		1,				/* bInterfaceNumber */
		0,				/* bAlternateSetting */
		1,				/* bNumEndpoints */
		14,				/* bInterfaceClass */
		0x02,				/* bInterfaceSubClass */
		0x00,				/* bInterfaceProtocol */
		1,				/* iInterface */
		&endpdesc_full[2],		/* endpoints */
		(void *)&video_streaming_descriptors,
		sizeof(video_streaming_descriptors)
	},
	{
		0
	}
};

/* Full-Speed settings */
static
struct SceUdcdInterfaceSettings settings_full[2] = {
	{
		&interdesc_full[0],
		0,
		1
	},
	{
		&interdesc_full[1],
		0,
		1
	}
};

/* Full-Speed configuration descriptor */
static
struct SceUdcdConfigDescriptor confdesc_full = {
	USB_DT_CONFIG_SIZE,
	USB_DT_CONFIG,
	(USB_DT_CONFIG_SIZE + 2 * USB_DT_INTERFACE_SIZE + 3 * USB_DT_ENDPOINT_SIZE +
		/* sizeof(interface_association_descriptor) + */
		sizeof(video_control_descriptors) +
		sizeof(video_streaming_descriptors)),	/* wTotalLength */
	2,			/* bNumInterfaces */
	1,			/* bConfigurationValue */
	0,			/* iConfiguration */
	0xC0,			/* bmAttributes */
	0,			/* bMaxPower */
	&settings_full[0],
	interface_association_descriptor,
	sizeof(interface_association_descriptor)
};

/* Full-Speed configuration */
static
struct SceUdcdConfiguration config_full = {
	&confdesc_full,
	&settings_full[0],
	&interdesc_full[0],
	&endpdesc_full[0]
};
