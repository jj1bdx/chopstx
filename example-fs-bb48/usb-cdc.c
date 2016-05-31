#include <stdint.h>
#include <stdlib.h>
#include <chopstx.h>
#include <string.h>
#include "board.h"
#include "usb_lld.h"
#include "tty.h"

static chopstx_intr_t usb_intr;

struct line_coding
{
  uint32_t bitrate;
  uint8_t format;
  uint8_t paritytype;
  uint8_t datatype;
}  __attribute__((packed));

static const struct line_coding line_coding0 = {
  115200, /* baud rate: 115200    */
  0x00,   /* stop bits: 1         */
  0x00,   /* parity:    none      */
  0x08    /* bits:      8         */
};

/*
 * Currently, we only support a single TTY.
 *
 * It is possible to extend to support multiple TTYs, for multiple
 * interfaces.
 *
 * In that case, add argument to TTY_OPEN function and
 * modify TTY_GET function to get the TTY structure.  Functions which
 * directy accesses TTY0 (usb_cb_device_reset and usb_cb_handle_event)
 * should be modified, too.
 *
 * Modification of TTY_MAIN thread will be also needed to echo back
 * input for each TTY, and the thread should run if one of TTY is
 * opened.
 */

struct tty {
  chopstx_mutex_t mtx;
  chopstx_cond_t cnd;
  uint8_t inputline[LINEBUFSIZE];   /* Line editing is supported */
  uint8_t send_buf[LINEBUFSIZE];    /* Sending ring buffer for echo back */
  uint8_t send_buf0[64];
  uint8_t recv_buf0[64];
  uint32_t inputline_len    : 8;
  uint32_t send_head        : 8;
  uint32_t send_tail        : 8;
  uint32_t flag_connected   : 1;
  uint32_t flag_send_ready  : 1;
  uint32_t flag_input_avail : 1;
  uint32_t                  : 2;
  uint32_t device_state     : 3;     /* USB device status */
  struct line_coding line_coding;
};

static struct tty tty0;

/*
 * Locate TTY structure from interface number or endpoint number.
 * Currently, it always returns tty0, because we only have the one.
 */
static struct tty *
tty_get (int interface, uint8_t ep_num)
{
  struct tty *t = &tty0;

  if (interface >= 0)
    {
      if (interface == 0)
	t = &tty0;
    }
  else
    {
      if (ep_num == ENDP1 || ep_num == ENDP2 || ep_num == ENDP3)
	t = &tty0;
    }

  return t;
}


#define USB_CDC_REQ_SET_LINE_CODING             0x20
#define USB_CDC_REQ_GET_LINE_CODING             0x21
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE      0x22
#define USB_CDC_REQ_SEND_BREAK                  0x23

/* USB Device Descriptor */
static const uint8_t vcom_device_desc[18] = {
  18,   /* bLength */
  DEVICE_DESCRIPTOR,		/* bDescriptorType */
  0x10, 0x01,			/* bcdUSB = 1.1 */
  0x02,				/* bDeviceClass (CDC).              */
  0x00,				/* bDeviceSubClass.                 */
  0x00,				/* bDeviceProtocol.                 */
  0x40,				/* bMaxPacketSize.                  */
  0xFF, 0xFF, /* idVendor  */
  0x01, 0x00, /* idProduct */
  0x00, 0x01, /* bcdDevice  */
  1,				/* iManufacturer.                   */
  2,				/* iProduct.                        */
  3,				/* iSerialNumber.                   */
  1				/* bNumConfigurations.              */
};

#define VCOM_FEATURE_BUS_POWERED	0x80

/* Configuration Descriptor tree for a CDC.*/
static const uint8_t vcom_config_desc[67] = {
  9,
  CONFIG_DESCRIPTOR,		/* bDescriptorType: Configuration */
  /* Configuration Descriptor.*/
  67, 0x00,			/* wTotalLength.                    */
  0x02,				/* bNumInterfaces.                  */
  0x01,				/* bConfigurationValue.             */
  0,				/* iConfiguration.                  */
  VCOM_FEATURE_BUS_POWERED,	/* bmAttributes.                    */
  50,				/* bMaxPower (100mA).               */
  /* Interface Descriptor.*/
  9,
  INTERFACE_DESCRIPTOR,
  0x00,		   /* bInterfaceNumber.                */
  0x00,		   /* bAlternateSetting.               */
  0x01,		   /* bNumEndpoints.                   */
  0x02,		   /* bInterfaceClass (Communications Interface Class,
		      CDC section 4.2).  */
  0x02,		   /* bInterfaceSubClass (Abstract Control Model, CDC
		      section 4.3).  */
  0x01,		   /* bInterfaceProtocol (AT commands, CDC section
		      4.4).  */
  0,	           /* iInterface.                      */
  /* Header Functional Descriptor (CDC section 5.2.3).*/
  5,	      /* bLength.                         */
  0x24,	      /* bDescriptorType (CS_INTERFACE).  */
  0x00,	      /* bDescriptorSubtype (Header Functional Descriptor). */
  0x10, 0x01, /* bcdCDC.                          */
  /* Call Management Functional Descriptor. */
  5,            /* bFunctionLength.                 */
  0x24,         /* bDescriptorType (CS_INTERFACE).  */
  0x01,         /* bDescriptorSubtype (Call Management Functional
		   Descriptor). */
  0x03,         /* bmCapabilities (D0+D1).          */
  0x01,         /* bDataInterface.                  */
  /* ACM Functional Descriptor.*/
  4,            /* bFunctionLength.                 */
  0x24,         /* bDescriptorType (CS_INTERFACE).  */
  0x02,         /* bDescriptorSubtype (Abstract Control Management
		   Descriptor).  */
  0x02,         /* bmCapabilities.                  */
  /* Union Functional Descriptor.*/
  5,            /* bFunctionLength.                 */
  0x24,         /* bDescriptorType (CS_INTERFACE).  */
  0x06,         /* bDescriptorSubtype (Union Functional
		   Descriptor).  */
  0x00,         /* bMasterInterface (Communication Class
		   Interface).  */
  0x01,         /* bSlaveInterface0 (Data Class Interface).  */
  /* Endpoint 2 Descriptor.*/
  7,
  ENDPOINT_DESCRIPTOR,
  ENDP2|0x80,    /* bEndpointAddress.    */
  0x03,          /* bmAttributes (Interrupt).        */
  0x08, 0x00,	 /* wMaxPacketSize.                  */
  0xFF,		 /* bInterval.                       */
  /* Interface Descriptor.*/
  9,
  INTERFACE_DESCRIPTOR, /* bDescriptorType: */
  0x01,          /* bInterfaceNumber.                */
  0x00,          /* bAlternateSetting.               */
  0x02,          /* bNumEndpoints.                   */
  0x0A,          /* bInterfaceClass (Data Class Interface, CDC section 4.5). */
  0x00,          /* bInterfaceSubClass (CDC section 4.6). */
  0x00,          /* bInterfaceProtocol (CDC section 4.7). */
  0x00,		 /* iInterface.                      */
  /* Endpoint 3 Descriptor.*/
  7,
  ENDPOINT_DESCRIPTOR,		/* bDescriptorType: Endpoint */
  ENDP3,    /* bEndpointAddress. */
  0x02,				/* bmAttributes (Bulk).             */
  0x40, 0x00,			/* wMaxPacketSize.                  */
  0x00,				/* bInterval.                       */
  /* Endpoint 1 Descriptor.*/
  7,
  ENDPOINT_DESCRIPTOR,		/* bDescriptorType: Endpoint */
  ENDP1|0x80,			/* bEndpointAddress. */
  0x02,				/* bmAttributes (Bulk).             */
  0x40, 0x00,			/* wMaxPacketSize.                  */
  0x00				/* bInterval.                       */
};


/*
 * U.S. English language identifier.
 */
static const uint8_t vcom_string0[4] = {
  4,				/* bLength */
  STRING_DESCRIPTOR,
  0x09, 0x04			/* LangID = 0x0409: US-English */
};

static const uint8_t vcom_string1[] = {
  23*2+2,			/* bLength */
  STRING_DESCRIPTOR,		/* bDescriptorType */
  /* Manufacturer: "Flying Stone Technology" */
  'F', 0, 'l', 0, 'y', 0, 'i', 0, 'n', 0, 'g', 0, ' ', 0, 'S', 0,
  't', 0, 'o', 0, 'n', 0, 'e', 0, ' ', 0, 'T', 0, 'e', 0, 'c', 0,
  'h', 0, 'n', 0, 'o', 0, 'l', 0, 'o', 0, 'g', 0, 'y', 0, 
};

static const uint8_t vcom_string2[] = {
  14*2+2,			/* bLength */
  STRING_DESCRIPTOR,		/* bDescriptorType */
  /* Product name: "Chopstx Sample" */
  'C', 0, 'h', 0, 'o', 0, 'p', 0, 's', 0, 't', 0, 'x', 0, ' ', 0,
  'S', 0, 'a', 0, 'm', 0, 'p', 0, 'l', 0, 'e', 0,
};

/*
 * Serial Number string.
 */
static const uint8_t vcom_string3[28] = {
  28,				    /* bLength */
  STRING_DESCRIPTOR,		    /* bDescriptorType */
  '0', 0,  '.', 0,  '0', 0, '0', 0, /* Version number */
};


#define NUM_INTERFACES 2


void
usb_cb_device_reset (void)
{
  usb_lld_reset (VCOM_FEATURE_BUS_POWERED);

  /* Initialize Endpoint 0 */
  usb_lld_setup_endp (ENDP0, 1, 1);

  chopstx_mutex_lock (&tty0.mtx);
  tty0.inputline_len = 0;
  tty0.send_head = tty0.send_tail = 0;
  tty0.flag_connected = 0;
  tty0.flag_send_ready = 1;
  tty0.flag_input_avail = 0;
  tty0.device_state = ATTACHED;
  memcpy (&tty0.line_coding, &line_coding0, sizeof (struct line_coding));
  chopstx_mutex_unlock (&tty0.mtx);
}


#define CDC_CTRL_DTR            0x0001

void
usb_cb_ctrl_write_finish (uint8_t req, uint8_t req_no, struct req_args *arg)
{
  uint8_t type_rcp = req & (REQUEST_TYPE|RECIPIENT);

  if (type_rcp == (CLASS_REQUEST | INTERFACE_RECIPIENT) && arg->index == 0
      && USB_SETUP_SET (req) && req_no == USB_CDC_REQ_SET_CONTROL_LINE_STATE)
    {
      struct tty *t = tty_get (arg->index, 0);

      /* Open/close the connection.  */
      chopstx_mutex_lock (&t->mtx);
      t->flag_connected = ((arg->value & CDC_CTRL_DTR) != 0);
      chopstx_cond_signal (&t->cnd);
      chopstx_mutex_unlock (&t->mtx);
    }
}



static int
vcom_port_data_setup (uint8_t req, uint8_t req_no, struct req_args *arg)
{
  if (USB_SETUP_GET (req))
    {
      struct tty *t = tty_get (arg->index, 0);

      if (req_no == USB_CDC_REQ_GET_LINE_CODING)
	return usb_lld_reply_request (&t->line_coding,
				      sizeof (struct line_coding), arg);
    }
  else  /* USB_SETUP_SET (req) */
    {
      if (req_no == USB_CDC_REQ_SET_LINE_CODING
	  && arg->len == sizeof (struct line_coding))
	{
	  struct tty *t = tty_get (arg->index, 0);

	  usb_lld_set_data_to_recv (&t->line_coding,
				    sizeof (struct line_coding));
	  return USB_SUCCESS;
	}
      else if (req_no == USB_CDC_REQ_SET_CONTROL_LINE_STATE)
	return USB_SUCCESS;
    }

  return USB_UNSUPPORT;
}

int
usb_cb_setup (uint8_t req, uint8_t req_no, struct req_args *arg)
{
  uint8_t type_rcp = req & (REQUEST_TYPE|RECIPIENT);

  if (type_rcp == (CLASS_REQUEST | INTERFACE_RECIPIENT) && arg->index == 0)
    return vcom_port_data_setup (req, req_no, arg);

  return USB_UNSUPPORT;
}

int
usb_cb_get_descriptor (uint8_t rcp, uint8_t desc_type, uint8_t desc_index,
		       struct req_args *arg)
{
  if (rcp != DEVICE_RECIPIENT)
    return USB_UNSUPPORT;

  if (desc_type == DEVICE_DESCRIPTOR)
    return usb_lld_reply_request (vcom_device_desc, sizeof (vcom_device_desc),
				  arg);
  else if (desc_type == CONFIG_DESCRIPTOR)
    return usb_lld_reply_request (vcom_config_desc, sizeof (vcom_config_desc),
				  arg);
  else if (desc_type == STRING_DESCRIPTOR)
    {
      const uint8_t *str;
      int size;

      switch (desc_index)
	{
	case 0:
	  str = vcom_string0;
	  size = sizeof (vcom_string0);
	  break;
	case 1:
	  str = vcom_string1;
	  size = sizeof (vcom_string1);
	  break;
	case 2:
	  str = vcom_string2;
	  size = sizeof (vcom_string2);
	  break;
	case 3:
	  str = vcom_string3;
	  size = sizeof (vcom_string3);
	  break;
	default:
	  return USB_UNSUPPORT;
	}

      return usb_lld_reply_request (str, size, arg);
    }

  return USB_UNSUPPORT;
}

static void
vcom_setup_endpoints_for_interface (uint16_t interface, int stop)
{
  if (interface == 0)
    {
      if (!stop)
	usb_lld_setup_endp (ENDP2, 0, 1);
      else
	usb_lld_stall (ENDP2);
    }
  else if (interface == 1)
    {
      if (!stop)
	{
	  usb_lld_setup_endp (ENDP1, 0, 1);
	  usb_lld_setup_endp (ENDP3, 1, 0);
	  /* Start with no data receiving (ENDP3 not enabled)*/
	}
      else
	{
	  usb_lld_stall (ENDP1);
	  usb_lld_stall (ENDP3);
	}
    }
}

int
usb_cb_handle_event (uint8_t event_type, uint16_t value)
{
  int i;
  uint8_t current_conf;

  switch (event_type)
    {
    case USB_EVENT_ADDRESS:
      chopstx_mutex_lock (&tty0.mtx);
      tty0.device_state = ADDRESSED;
      chopstx_mutex_unlock (&tty0.mtx);
      return USB_SUCCESS;
    case USB_EVENT_CONFIG:
      current_conf = usb_lld_current_configuration ();
      if (current_conf == 0)
	{
	  if (value != 1)
	    return USB_UNSUPPORT;

	  usb_lld_set_configuration (1);
	  for (i = 0; i < NUM_INTERFACES; i++)
	    vcom_setup_endpoints_for_interface (i, 0);
	  chopstx_mutex_lock (&tty0.mtx);
	  tty0.device_state = CONFIGURED;
	  chopstx_mutex_unlock (&tty0.mtx);
	}
      else if (current_conf != value)
	{
	  if (value != 0)
	    return USB_UNSUPPORT;

	  usb_lld_set_configuration (0);
	  for (i = 0; i < NUM_INTERFACES; i++)
	    vcom_setup_endpoints_for_interface (i, 1);
	  chopstx_mutex_lock (&tty0.mtx);
	  tty0.device_state = ADDRESSED;
	  chopstx_mutex_unlock (&tty0.mtx);
	}
      /* Do nothing when current_conf == value */
      return USB_SUCCESS;
    default:
      break;
    }

  return USB_UNSUPPORT;
}


int
usb_cb_interface (uint8_t cmd, struct req_args *arg)
{
  const uint8_t zero = 0;
  uint16_t interface = arg->index;
  uint16_t alt = arg->value;

  if (interface >= NUM_INTERFACES)
    return USB_UNSUPPORT;

  switch (cmd)
    {
    case USB_SET_INTERFACE:
      if (alt != 0)
	return USB_UNSUPPORT;
      else
	{
	  vcom_setup_endpoints_for_interface (interface, 0);
	  return USB_SUCCESS;
	}

    case USB_GET_INTERFACE:
      return usb_lld_reply_request (&zero, 1, arg);

    default:
    case USB_QUERY_INTERFACE:
      return USB_SUCCESS;
    }
}


/*
 * Put a character into the ring buffer to be send back.
 */
static void
put_char_to_ringbuffer (struct tty *t, int c)
{
  uint32_t next = (t->send_tail + 1) % LINEBUFSIZE;

  if (t->send_head == next)
    /* full */
    /* All that we can do is ignore this char. */
    return;
  
  t->send_buf[t->send_tail] = c;
  t->send_tail = next;
}

/*
 * Get characters from ring buffer into S.
 */
static int
get_chars_from_ringbuffer (struct tty *t, uint8_t *s, int len)
{
  int i = 0;

  if (t->send_head == t->send_tail)
    /* Empty */
    return i;

  do
    {
      s[i++] = t->send_buf[t->send_head];
      t->send_head = (t->send_head + 1) % LINEBUFSIZE;
    }
  while (t->send_head != t->send_tail && i < len);

  return i;
}


static void
tty_echo_char (struct tty *t, int c)
{
  put_char_to_ringbuffer (t, c);
}

void
usb_cb_tx_done (uint8_t ep_num, uint32_t len, int success)
{
  struct tty *t = tty_get (-1, ep_num);

  (void)len;
  (void)success;		/* Always, successful.  */

  if (ep_num == ENDP1)
    {
      chopstx_mutex_lock (&t->mtx);
      if (t->flag_send_ready == 0)
	{
	  t->flag_send_ready = 1;
	  chopstx_cond_signal (&t->cnd);
	}
      chopstx_mutex_unlock (&t->mtx);
    }
  else if (ep_num == ENDP2)
    {
      /* Nothing */
    }
}


static int
tty_input_char (struct tty *t, int c)
{
  unsigned int i;
  int r = 0;

  /* Process DEL, C-U, C-R, and RET as editing command. */
  chopstx_mutex_lock (&t->mtx);
  switch (c)
    {
    case 0x0d: /* Control-M */
      t->inputline[t->inputline_len++] = '\n';
      tty_echo_char (t, 0x0d);
      tty_echo_char (t, 0x0a);
      t->flag_input_avail = 1;
      r = 1;
      chopstx_cond_signal (&t->cnd);
      break;
    case 0x12: /* Control-R */
      tty_echo_char (t, '^');
      tty_echo_char (t, 'R');
      tty_echo_char (t, 0x0d);
      tty_echo_char (t, 0x0a);
      for (i = 0; i < t->inputline_len; i++)
	tty_echo_char (t, t->inputline[i]);
      break;
    case 0x15: /* Control-U */
      for (i = 0; i < t->inputline_len; i++)
	{
	  tty_echo_char (t, 0x08);
	  tty_echo_char (t, 0x20);
	  tty_echo_char (t, 0x08);
	}
      t->inputline_len = 0;
      break;
    case 0x7f: /* DEL    */
      if (t->inputline_len > 0)
	{
	  tty_echo_char (t, 0x08);
	  tty_echo_char (t, 0x20);
	  tty_echo_char (t, 0x08);
	  t->inputline_len--;
	}
      break;
    default:
      if (t->inputline_len < sizeof (t->inputline) - 1)
	{
	  tty_echo_char (t, c);
	  t->inputline[t->inputline_len++] = c;
	}
      else
	/* Beep */
	tty_echo_char (t, 0x0a);
      break;
    }
  chopstx_mutex_unlock (&t->mtx);
  return r;
}

void
usb_cb_rx_ready (uint8_t ep_num)
{
  struct tty *t = tty_get (-1, ep_num);

  if (ep_num == ENDP3)
    {
      int i, r;

      r = usb_lld_rx_data_len (ENDP3);
      for (i = 0; i < r; i++)
	if (tty_input_char (t, t->recv_buf0[i]))
	  break;

      chopstx_mutex_lock (&t->mtx);
      if (t->flag_input_avail == 0)
	usb_lld_rx_enable_buf (ENDP3, t->recv_buf0, 64);
      chopstx_mutex_unlock (&t->mtx);
    }
}

static void *tty_main (void *arg);

#define INTR_REQ_USB 24
#define PRIO_TTY      4

extern uint8_t __process3_stack_base__, __process3_stack_size__;
const uint32_t __stackaddr_tty = (uint32_t)&__process3_stack_base__;
const size_t __stacksize_tty = (size_t)&__process3_stack_size__;

struct tty *
tty_open (void)
{
  chopstx_mutex_init (&tty0.mtx);
  chopstx_cond_init (&tty0.cnd);
  tty0.inputline_len = 0;
  tty0.send_head = tty0.send_tail = 0;
  tty0.flag_connected = 0;
  tty0.flag_send_ready = 1;
  tty0.flag_input_avail = 0;
  tty0.device_state = UNCONNECTED;
  memcpy (&tty0.line_coding, &line_coding0, sizeof (struct line_coding));

  chopstx_create (PRIO_TTY, __stackaddr_tty, __stacksize_tty, tty_main, &tty0);
  return &tty0;
}


static void *
tty_main (void *arg)
{
  struct tty *t = arg;

#if defined(OLDER_SYS_H)
  /*
   * Historically (before sys < 3.0), NVIC priority setting for USB
   * interrupt was done in usb_lld_sys_init.  Thus this code.
   *
   * When USB interrupt occurs between usb_lld_init (which assumes
   * ISR) and chopstx_claim_irq (which clears pending interrupt),
   * invocation of usb_interrupt_handler won't occur.
   *
   * Calling usb_interrupt_handler is no harm even if there were no
   * interrupts, thus, we call it unconditionally here, just in case
   * if there is a request.
   *
   * We can't call usb_lld_init after chopstx_claim_irq, as
   * usb_lld_init does its own setting for NVIC.  Calling
   * chopstx_claim_irq after usb_lld_init overrides that.
   *
   */
  usb_lld_init (VCOM_FEATURE_BUS_POWERED);
  chopstx_claim_irq (&usb_intr, INTR_REQ_USB);
  usb_interrupt_handler ();
#else
  chopstx_claim_irq (&usb_intr, INTR_REQ_USB);
  usb_lld_init (VCOM_FEATURE_BUS_POWERED);
#endif

  while (1)
    {
      chopstx_poll (NULL, 1, &usb_intr);
      if (usb_intr.ready)
	usb_interrupt_handler ();

      chopstx_mutex_lock (&t->mtx);
      if (t->device_state == CONFIGURED && t->flag_connected
	  && t->flag_send_ready)
	{
	  uint8_t line[32];
	  int len = get_chars_from_ringbuffer (t, line, sizeof (len));

	  if (len)
	    {
	      memcpy (t->send_buf0, line, len);
	      usb_lld_tx_enable_buf (ENDP1, t->send_buf0, len);
	      t->flag_send_ready = 0;
	    }
	}
      chopstx_mutex_unlock (&t->mtx);
    }

  return NULL;
}


void
tty_wait_configured (struct tty *t)
{
  chopstx_mutex_lock (&t->mtx);
  while (t->device_state != CONFIGURED)
    chopstx_cond_wait (&t->cnd, &t->mtx);
  chopstx_mutex_unlock (&t->mtx);
}


void
tty_wait_connection (struct tty *t)
{
  chopstx_mutex_lock (&t->mtx);
  while (t->flag_connected == 0)
    chopstx_cond_wait (&t->cnd, &t->mtx);
  t->flag_send_ready = 1;
  t->flag_input_avail = 0;
  t->send_head = t->send_tail = 0;
  t->inputline_len = 0;
  /* Accept input for line */
  usb_lld_rx_enable_buf (ENDP3, t->recv_buf0, 64);
  chopstx_mutex_unlock (&t->mtx);
}

static int
check_tx (struct tty *t)
{
  if (t->flag_send_ready)
    /* TX done */
    return 1;
  if (t->flag_connected == 0)
    /* Disconnected */
    return -1;
  return 0;
}

int
tty_send (struct tty *t, const uint8_t *buf, int len)
{
  int r;
  const uint8_t *p;
  int count;

  p = buf;
  count = len >= 64 ? 64 : len;

  while (1)
    {
      chopstx_mutex_lock (&t->mtx);
      while ((r = check_tx (t)) == 0)
	chopstx_cond_wait (&t->cnd, &t->mtx);
      if (r > 0)
	{
	  usb_lld_tx_enable_buf (ENDP1, p, count);
	  t->flag_send_ready = 0;
	}
      chopstx_mutex_unlock (&t->mtx);

      len -= count;
      p += count;
      if (len == 0 && count != 64)
	/*
	 * The size of the last packet should be != 0
	 * If 64, send ZLP (zelo length packet)
	 */
	break;
      count = len >= 64 ? 64 : len;
    }

  /* Wait until all sent. */
  chopstx_mutex_lock (&t->mtx);
  while ((r = check_tx (t)) == 0)
    chopstx_cond_wait (&t->cnd, &t->mtx);
  chopstx_mutex_unlock (&t->mtx);
  return r;
}


static int
check_rx (void *arg)
{
  struct tty *t = arg;

  if (t->flag_input_avail)
    /* RX */
    return 1;
  if (t->flag_connected == 0)
    /* Disconnected */
    return 1;
  return 0;
}

/*
 * Returns -1 on connection close
 *          0 on timeout.
 *          >0 length of the inputline (including final \n) 
 *
 */
int
tty_recv (struct tty *t, uint8_t *buf, uint32_t *timeout)
{
  int r;
  chopstx_poll_cond_t poll_desc;

  poll_desc.type = CHOPSTX_POLL_COND;
  poll_desc.ready = 0;
  poll_desc.cond = &t->cnd;
  poll_desc.mutex = &t->mtx;
  poll_desc.check = check_rx;
  poll_desc.arg = t;

  while (1)
    {
      chopstx_poll (timeout, 1, &poll_desc);
      chopstx_mutex_lock (&t->mtx);
      r = check_rx (t);
      chopstx_mutex_unlock (&t->mtx);
      if (r || (timeout != NULL && *timeout == 0))
	break;
    }

  chopstx_mutex_lock (&t->mtx);
  if (t->flag_connected == 0)
    r = -1;
  else if (t->flag_input_avail)
    {
      r = t->inputline_len;
      memcpy (buf, t->inputline, r);
      t->flag_input_avail = 0;
      usb_lld_rx_enable_buf (ENDP3, t->recv_buf0, 64);
      t->inputline_len = 0;
    }
  else
    r = 0;
  chopstx_mutex_unlock (&t->mtx);

  return r;
}
