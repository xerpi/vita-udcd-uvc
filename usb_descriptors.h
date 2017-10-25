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

static
unsigned char video_control_descriptors[] = {
        /* Class specific VC Interface Header Descriptor */
        0x0D,                           /* Descriptor size */
        0x24,                           /* Class Specific I/f Header Descriptor type */
        0x01,                           /* Descriptor Sub type : VC_HEADER */
        0x10, 0x01,                     /* Revision of UVC class spec: 1.1 - Minimum version required
                                           for USB Compliance. Not supported on Windows XP*/
        0x51, 0x00,                     /* Total Size of class specific descriptors (till Output terminal) */
        0x00,0x6C,0xDC,0x02,            /* Clock frequency : 48MHz(Deprecated) */
        0x01,                           /* Number of streaming interfaces */
        0x01,                           /* Video streaming I/f 1 belongs to VC i/f */

        /* Input (Camera) Terminal Descriptor */
        0x12,                           /* Descriptor size */
        0x24,                           /* Class specific interface desc type */
        0x02,                           /* Input Terminal Descriptor type */
        CAMERA_TERMINAL_ID,             /* ID of this terminal */
        0x01,0x02,                      /* Camera terminal type */
        0x00,                           /* No association terminal */
        0x00,                           /* String desc index : Not used */
        0x00,0x00,                      /* No optical zoom supported */
        0x00,0x00,                      /* No optical zoom supported */
        0x00,0x00,                      /* No optical zoom supported */
        0x03,                           /* Size of controls field for this terminal : 3 bytes */
        0x00,0x00,0x00,                 /* bmControls field of camera terminal: No controls supported */

        /* Processing Unit Descriptor */
        0x0D,                           /* Descriptor size */
        0x24,                           /* Class specific interface desc type */
        0x05,                           /* Processing Unit Descriptor type */
        PROCESSING_UNIT_ID,             /* ID of this terminal */
        CAMERA_TERMINAL_ID,             /* Source ID : 1 : Conencted to input terminal */
        0x00,0x40,                      /* Digital multiplier */
        0x03,                           /* Size of controls field for this terminal : 3 bytes */
        0x00,0x00,0x00,                 /* bmControls field of processing unit: Brightness control supported */
        0x00,                           /* String desc index : Not used */
        0x00,                           /* Analog Video Standards Supported: None */

        /* Extension Unit Descriptor */
        0x1C,                           /* Descriptor size */
        0x24,                           /* Class specific interface desc type */
        0x06,                           /* Extension Unit Descriptor type */
        EXTENSION_UNIT_ID,              /* ID of this terminal */
        0xFF,0xFF,0xFF,0xFF,            /* 16 byte GUID */
        0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,
        0x00,                           /* Number of controls in this terminal */
        0x01,                           /* Number of input pins in this terminal */
        PROCESSING_UNIT_ID,             /* Source ID : 2 : Connected to Proc Unit */
        0x03,                           /* Size of controls field for this terminal : 3 bytes */
        0x00,0x00,0x00,                 /* No controls supported */
        0x00,                           /* String desc index : Not used */

        /* Output Terminal Descriptor */
        0x09,                           /* Descriptor size */
        0x24,                           /* Class specific interface desc type */
        0x03,                           /* Output Terminal Descriptor type */
        OUTPUT_TERMINAL_ID,             /* ID of this terminal */
        0x01,0x01,                      /* USB Streaming terminal type */
        0x00,                           /* No association terminal */
        EXTENSION_UNIT_ID,              /* Source ID : 3 : Connected to Extn Unit */
        0x00,                           /* String desc index : Not used */
};

static
unsigned char video_control_specific_endpoint_descriptors[] = {
	/* Class-specific Interrupt Endpoint Descriptor */
	0x05,                           /* Descriptor size */
	0x25,                           /* Class Specific Endpoint Descriptor Type */
	3,                              /* End point Sub Type */
	0x40,0x00,                      /* Max packet size = 64 bytes */
};

static
unsigned char video_streaming_descriptors[] = {
	/* Class-specific Video Streaming Input Header Descriptor */
	0x0E,			/* Descriptor size */
	0x24,			/* Class-specific VS I/f Type */
	0x01,			/* Descriptotor Subtype : Input Header */
	0x01,			/* No format descriptor supported for FS device */
	0x0E, 0x00,		/* Total size of Class specific VS descr */
	0x83,			/* EP address for BULK video data */
	0x00,			/* No dynamic format change supported */
	OUTPUT_TERMINAL_ID,	/* Output terminal ID : 4 */
	0x01,			/* Still image capture method 1 supported */
	0x00,			/* Hardware trigger NOT supported */
	0x00,			/* Hardware to initiate still image capture NOT supported */
	0x01,			/* Size of controls field : 1 byte */
	0x00,			/* D2 : Compression quality supported */

	/* Class specific Uncompressed VS Format descriptor */
	0x1B,                           /* Descriptor size */
	0x24,                           /* Class-specific VS I/f Type */
	0x04,                           /* Subtype : uncompressed format I/F */
	0x01,                           /* Format desciptor index (only one format is supported) */
	0x01,                           /* number of frame descriptor followed */
	0x59,0x55,0x59,0x32,            /* GUID used to identify streaming-encoding format: YUY2  */
	0x00,0x00,0x10,0x00,
	0x80,0x00,0x00,0xAA,
	0x00,0x38,0x9B,0x71,
	0x10,                           /* Number of bits per pixel used to specify color in the decoded video frame.
				           0 if not applicable: 16 bit per pixel */
	0x01,                           /* Optimum Frame Index for this stream: 1 */
	0x08,                           /* X dimension of the picture aspect ratio: Non-interlaced in progressive scan */
	0x06,                           /* Y dimension of the picture aspect ratio: Non-interlaced in progressive scan*/
	0x00,                           /* Interlace Flags: Progressive scanning, no interlace */
	0x00,                           /* duplication of the video stream restriction: 0 - no restriction */

	/* Class specific Uncompressed VS Frame descriptor */
	0x1E,                           /* Descriptor size */
	0x24,                           /* Descriptor type*/
	0x05,                           /* Subtype: uncompressed frame I/F */
	0x01,                           /* Frame Descriptor Index */
	0x01,                           /* Still image capture method 1 supported */
	0x80,0x02,                      /* Width in pixel: 320-QVGA */
	0xE0,0x01,                      /* Height in pixel 240-QVGA */
	0x00,0x50,0x97,0x31,            /* Min bit rate bits/s. Not specified, taken from MJPEG */
	0x00,0x50,0x97,0x31,            /* Max bit rate bits/s. Not specified, taken from MJPEG */
	0x00,0x60,0x09,0x00,            /* Maximum video or still frame size in bytes(Deprecated) */
	0x2A,0x2C,0x0A,0x00,            /* Default Frame Interval */
	0x01,                           /* Frame interval(Frame Rate) types: Only one frame interval supported */
	0x2A,0x2C,0x0A,0x00,            /* Shortest Frame Interval */
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

/* String descriptor */
static
struct SceUdcdStringDescriptor string_descriptors[2] = {
	{
		18,
		USB_DT_STRING,
		{'V', 'i', 't', 'a', ' ', 'U', 'V', 'C'}
	},
	{
		0,
		USB_DT_STRING
	}
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
	0,			/* iProduct */
	0,			/* iSerialNumber */
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
		video_control_descriptors,
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
		video_streaming_descriptors,
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
	0x200,			/* bcdDevice */
	0,			/* iManufacturer */
	0,			/* iProduct */
	0,			/* iSerialNumber */
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
		video_control_descriptors,
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
		video_streaming_descriptors,
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
