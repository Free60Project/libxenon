/*  *********************************************************************
	*  Broadcom Common Firmware Environment (CFE)
	*  
	*  USB Human Interface Driver				File: usb.c
	*  
	*  Xbox 360 Controller support
	*  
	*  Author:  Felix Domke (based on usbhid.c)
	*  
	*  Multi wirless controller support hacked in by slasher - this is beta status at this point in time
	*********************************************************************  
	*
	*  Copyright 2000,2001,2002,2003,2005
	*  Broadcom Corporation. All rights reserved.
	*  
	*  This software is furnished under license and may be used and 
	*  copied only in accordance with the following terms and 
	*  conditions.  Subject to these conditions, you may download, 
	*  copy, install, use, modify and distribute modified or unmodified 
	*  copies of this software in source and/or binary form.  No title 
	*  or ownership is transferred hereby.
	*  
	*  1) Any source code used, modified or distributed must reproduce 
	*	 and retain this copyright notice and list of conditions 
	*	 as they appear in the source file.
	*  
	*  2) No right is granted to use any trade name, trademark, or 
	*	 logo of Broadcom Corporation.  The "Broadcom Corporation" 
	*	 name may not be used to endorse or promote products derived 
	*	 from this software without the prior written permission of 
	*	 Broadcom Corporation.
	*  
	*  3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR
	*	 IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED
	*	 WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR 
	*	 PURPOSE, OR NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT 
	*	 SHALL BROADCOM BE LIABLE FOR ANY DAMAGES WHATSOEVER, AND IN 
	*	 PARTICULAR, BROADCOM SHALL NOT BE LIABLE FOR DIRECT, INDIRECT,
	*	 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
	*	 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
	*	 GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
	*	 BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
	*	 OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR 
	*	 TORT (INCLUDING NEGLIGENCE OR OTHERWISE), EVEN IF ADVISED OF 
	*	 THE POSSIBILITY OF SUCH DAMAGE.
	********************************************************************* */

#include <input/input.h>
#include "cfe.h"
#include <xenon_smc/xenon_smc.h>


#include "usbchap9.h"
#include "usbd.h"

/* For systems with non-coherent DMA, allocate all buffers to be
   cache-aligned and multiples of a cache line in size, so that they
   can be safely flushed or invalidated. */

#define CACHE_ALIGN	32	   /* XXX place holder, big enough to now. */
#define BUFF_ALIGN	 16
#define ALIGN(n,align) (((n)+((align)-1)) & ~((align)-1))

#define usb_dma_alloc(n) (KMALLOC(ALIGN((n),CACHE_ALIGN),BUFF_ALIGN))
#define usb_dma_free(p)  (KFREE(p))


static int usbctrl_attach(usbdev_t *dev,usb_driver_t *drv);
static int usbctrl_detach(usbdev_t *dev);

#define UBR_KBD_MAX 20

static int controller_mask = 0;

usbdev_t *RFdev;

typedef struct usbctrl_softc_s {
	int uhid_ipipe;
	int uhid_ipipe_tx;
	int uhid_ipipemps;
	int uhid_ipipemps_tx;
	int uhid_devtype;
	int is_wireless, index, wireless_index;
	uint8_t *uhid_imsg;
	uint8_t uhid_lastmsg[UBR_KBD_MAX];
	uint32_t uhid_shiftflags;
	usbdev_t* dev;
} usbctrl_softc_t;

static usbctrl_softc_t* controllers[4] = {NULL, NULL, NULL, NULL};

static void setcontroller(usbctrl_softc_t* ctrl, int id) //FIXME: Not exactly sure if I like this way of finding a controller from a controller num, possibly rumble should be done through controller data and be updated periodically
{
	if(id >= 0 && id < 4)
		controllers[id] = ctrl;
}
static usbctrl_softc_t* getcontroller(int id)
{
	if(id >= 0 && id < 4)
		return controllers[id];
	return 0;
}

usb_driver_t usbctrl_driver = {
	"Xbox 360 Controller",
	usbctrl_attach,
	usbctrl_detach
};

static int usbctrl_set_led_callback(usbreq_t *ur) {
    usb_dma_free(ur->ur_buffer); //Don't leak the buffer
    //printf("Got callback for set leds\n");
    usb_free_request(ur);
    return 0;
}

int usbctrl_set_rol(uint controllerMask)
{
	if(!RFdev)
		return -1;
	return usb_simple_request(RFdev, 0x40, 0x02, controllerMask, 0x00);
}

int usbctrl_set_leds(usbctrl_softc_t * softc, uint8_t clear) {
    usbdev_t *dev = softc->dev;
    usbreq_t *ur;

    //xenon_smc_set_led(1, (unsigned char) (0x80 >> 3)); //We need to figure out how to set ROL and still have the sync button work

    unsigned xdata_len = 0;
    int command;
    if(!clear)
    {
    command = 2 + (softc->index % 4);
    }
    else
    {
    command = 0;
    }
    
    uint8_t* buf = NULL;


    if (softc->is_wireless) 
    {
	uint8_t xdata[0xC] = { 0x00, 0x00, 0x08, 0x40 + (command % 0x0e), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        xdata_len = 0xC;
	
	buf = usb_dma_alloc(0xC);
    	if (buf == NULL) {
		printf("couldn't alloc buffer\n");
		/* Could not allocate a buffer, fail. */
		return -1;
	}

	memcpy(buf, &xdata, 0xC);
	
    }
    else 
    {
	uint8_t xdata[3] = { 0x01, 0x03, command };
        xdata_len = 3;

	buf = usb_dma_alloc(3);
    	if (buf == NULL) {
		printf("couldn't alloc buffer\n");
		/* Could not allocate a buffer, fail. */
		return -1;
	}

	memcpy(buf, &xdata, 3);
    }

    ur = usb_make_request(dev, softc->uhid_ipipe_tx, buf, xdata_len, UR_FLAG_OUT);
    ur->ur_callback = usbctrl_set_led_callback;
    usb_queue_request(ur);

    return 0;
}

int usbctrl_poll(usbctrl_softc_t * softc) {
    usbdev_t *dev = softc->dev;
    usbreq_t *ur;
    unsigned xdata_len = 0;

    uint8_t* buf = NULL;


    if (softc->is_wireless) 
    {
	uint8_t xdata[0xC] = { 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };//Request headset status command, works as a poll
        xdata_len = 0xC;
	
	buf = usb_dma_alloc(0xC);
    	if (buf == NULL) {
		printf("couldn't alloc buffer\n");
		/* Could not allocate a buffer, fail. */
		return -1;
	}
	memcpy(buf, &xdata, 0xC);
	
    }
    else 
    {
	return -1; //Polling not needed for wired controllers, and if there is a command it is not known at this time
    }

    ur = usb_make_request(dev, softc->uhid_ipipe_tx, buf, xdata_len, UR_FLAG_OUT);
    ur->ur_callback = usbctrl_set_led_callback;
    usb_queue_request(ur);

    return 0;
}

int usbctrl_set_rumble(int port, uint8_t l, uint8_t r) {
    usbctrl_softc_t * softc = getcontroller(port);
    if(softc == NULL)
	return -1;
    usbdev_t *dev = softc->dev;
    usbreq_t *ur;
    unsigned xdata_len;
    uint8_t* buf = NULL;

    if(softc->is_wireless)
    {
	buf = usb_dma_alloc(0xC);

	if (buf == NULL) {
		printf("couldn't alloc buffer\n");
		/* Could not allocate a buffer, fail. */
		return -1;
	}

    	uint8_t xdata[0xC] = { 0x00, 0x01, 0x0f, 0xc0, 0x00, l, r, 0x00, 0x00, 0x00, 0x00, 0x00 };
	xdata_len = 0xC;
	memcpy(buf, &xdata, 0xC);
    }
    else
    {
	buf = usb_dma_alloc(0x8);
	 
	if (buf == NULL) {
		printf("couldn't alloc buffer\n");
		/* Could not allocate a buffer, fail. */
		return -1;
	}

	uint8_t xdata[0x8] = { 0x00, 0x08, 0x00, l, r, 0x00, 0x00, 0x00 };
	xdata_len = 0x8;
	memcpy(buf, &xdata, 0x8);
    }



    ur = usb_make_request(dev, softc->uhid_ipipe_tx, buf, xdata_len, UR_FLAG_OUT);
    ur->ur_callback = usbctrl_set_led_callback;
    usb_queue_request(ur);

    return 0;
}




/*  *********************************************************************
	*  usbctrl_ireq_callback(ur)
	*  
	*  This routine is called when our interrupt transfer completes
	*  and there is report data to be processed.
	*  
	*  Input parameters: 
	*			 ur - usb request
	*			 
	*  Return value:
	*			 0
	********************************************************************* */


// from wireless:
//08 80 82 00 40 01 02 20 00 07 05 82 03 20 00 02 07 05 02 03 
//00 0f 00 f0 f0 cc 56 57 9e c0 53 7f c9 00 00 05 13 e7 20 1d 
//00 00 00 f0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
//00 f8 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
//00 00 00 13 e2 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
//00 00 00 f0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
//00 01 00 f0 00 13 00 00 00 00 4f fc 1e 06 88 07 f6 f7 00 00 
//00 00 00 f0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
//00 01 00 f0 00 13 00 00 00 00 9d fd 1e 06 88 07 f6 f7 00 00 
//00 00 00 f0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 
//00 01 00 f0 00 13 00 00 00 00 9d fd ce 04 88 07 f6 f7 00 00 

static int usbctrl_ireq_callback(usbreq_t *ur)
{
	usbctrl_softc_t *uhid = (ur->ur_ref); //FIXME: are we allowed to use ur_ref for this purpose?
	
/*	int i;
	for (i = 0; i < uhid->uhid_ipipemps; ++i)
		printf("%02x ", ur->ur_buffer[i]);
	printf("\n");  */
	
	/*
	 * If the driver is unloaded, the request will be cancelled.
	 */

	if (ur->ur_status == 0xFF) {
		usb_free_request(ur);
		return 0;
	}

	/*
	printf("Dump:\n");
	int i;
	for (i = 0; i < uhid->uhid_ipipemps; ++i)
		printf("%02x ", ur->ur_buffer[i]);
	printf("\n\n\n");
	*/


	struct controller_data_s c;
	unsigned char *b = ur->ur_buffer;
	
	if (uhid->is_wireless)
	{
		//FIXME: A polled, disconnected controller may cause uneeded spam of the controller status message
		if(uhid->index == -1)
		{
			//printf("Wireless controller %i has connected\n", uhid->wireless_index);
			int i;
			for (i = 0; controller_mask & (1<<i); ++i);
			//printf("attached controller %d\n", i);
			uhid->index = i;
			setcontroller(uhid, uhid->index);
			controller_mask |= 1<<i;
			
			usbctrl_set_leds(uhid, 0);
			usbctrl_set_rol(controller_mask);
			
		}

		if (b[0] == 0x8 && b[1] == 0x0)
		{
			//printf("Wireless controller %i has disconnected\n", uhid->wireless_index);
			printf("detached controller %d\n", uhid->index);
			setcontroller(NULL, uhid->index);
			controller_mask &= ~(1<<uhid->index);

			usbctrl_set_rol(controller_mask);

			set_controller_type(uhid->index, CTRL_TYPE_NONE);

			uhid->index = -1;
			goto ignore;

		}

		if (b[0] == 0x0 && b[5] == 0xCC)
		{
			unsigned char pad_type = b[25] & 0x7F;
			unsigned short pad_vendor = ((b[22] & 0xf) | b[24] << 4) << 8 | b[23];

			set_controller_type(uhid->index, pad_type);

			goto ignore;
		}



		if (b[5] == 0x13)
		{
#if 0
			if (b[6] == 2) /* CHECKME: is this required for the Xbox racing wheel? */
				b++;
#endif				
			b += 4;
			
		} else
			goto ignore;
	}

	c.s1_x = (b[7] << 8) | b[6];
	c.s1_y = (b[9] << 8) | b[8];
	c.s2_x = (b[11] << 8) | b[10];
	c.s2_y = (b[13] << 8) | b[12];
	c.s1_z = !!(b[2] & 0x40);
	c.s2_z = !!(b[2] & 0x80);
	c.lt = b[4];
	c.rt = b[5];
	c.lb = !!(b[3] & 1);
	c.rb = !!(b[3] & 2);
	
	c.a = !!(b[3] & 0x10);
	c.b = !!(b[3] & 0x20);
	c.x = !!(b[3] & 0x40);
	c.y = !!(b[3] & 0x80);
	
	c.start = !!(b[2] & 0x10);
	c.back  = !!(b[2] & 0x20);
	
	c.up = !!(b[2] & 1);
	c.down = !!(b[2] & 2);
	c.left = !!(b[2] & 4);
	c.right = !!(b[2] & 8);
	
	c.logo = !!(b[3] & 0x4);
	
	set_controller_data(uhid->index, &c);

ignore:
	usb_queue_request(ur);

	return 0;
}


/*  *********************************************************************
	*  usbctrl_queue_intreq(dev,softc)
	*  
	*  Queue an interrupt request for this usb device.  The
	*  driver will place this request on the queue that corresponds
	*  to the endpoint, and will call the callback routine when
	*  something happens.
	*  
	*  Input parameters: 
	*			 dev - usb device
	*			 softc - the usb hid softc
	*			 
	*  Return value:
	*			 nothing
	********************************************************************* */

static void usbctrl_queue_intreq(usbdev_t *dev,usbctrl_softc_t *softc)
{
	usbreq_t *ur;

	ur = usb_make_request(dev,
						  softc->uhid_ipipe,
						  softc->uhid_imsg, softc->uhid_ipipemps,
						  UR_FLAG_IN);

	ur->ur_callback = usbctrl_ireq_callback;

	ur->ur_ref = softc; //FIXME: Part of the hackish multi-wireless support

	usb_queue_request(ur);
}


/*  *********************************************************************
	*  usbctrl_attach(dev,drv)
	*  
	*  This routine is called when the bus scan stuff finds a HID
	*  device.  We finish up the initialization by configuring the
	*  device and allocating our softc here.
	*  
	*  Input parameters: 
	*			 dev - usb device, in the "addressed" state.
	*			 drv - the driver table entry that matched
	*			 
	*  Return value:
	*			 0
	********************************************************************* */

static int usbctrl_attach(usbdev_t *dev,usb_driver_t *drv)
{
	usb_config_descr_t *cfgdscr = dev->ud_cfgdescr;
	//printf("Number of interfaces: %i\n", cfgdscr->bNumInterfaces);



	dev->ud_drv = drv;

	usbctrl_softc_t *softc;

	



	//printf("Number of endpoints: %i\n", ifdscr->bNumEndpoints);

	usb_endpoint_descr_t *epdscr;
	usb_interface_descr_t *ifdscr;

	int wireless = (GETUSBFIELD(&dev->ud_devdescr, idProduct) == 0x291) || 
			(GETUSBFIELD(&dev->ud_devdescr, idProduct) == 0x2aa) ||
			(GETUSBFIELD(&dev->ud_devdescr, idProduct) == 0x2a9);

	if(wireless)
	{

		dev->ud_private = NULL;
		RFdev = dev;

		int i;
		for(i = 0; i < 4; i++)
		{
			softc = KMALLOC(sizeof(usbctrl_softc_t),0);
			softc->dev = dev;
			softc->is_wireless = (wireless);
			epdscr = usb_find_cfg_descr(dev,USB_ENDPOINT_DESCRIPTOR_TYPE,i * 4);
			ifdscr = usb_find_cfg_descr(dev,USB_INTERFACE_DESCRIPTOR_TYPE,i * 2);

			if (!epdscr || !ifdscr) {
				printf("couldn't find descriptor for controller %i!\n", i);
				return 0;
			}
		

			//printf("Initializing wireless controller %d\n", i);
			softc->index = -1;
			softc->wireless_index = i;
			//controller_mask |= 1<<i; //Dont set this right now, let wired controllers use the slots

			/*
			 * Allocate a DMA buffer
			 */

			softc->uhid_imsg = usb_dma_alloc(UBR_KBD_MAX);
			if (softc->uhid_imsg == NULL) {
				printf("couldn't alloc buffer\n");
				/* Could not allocate a buffer, fail. */
				return -1;
			}
	
			/*
			 * Choose the standard configuration.
	 		*/

			usb_set_configuration(dev,cfgdscr->bConfigurationValue);

			/*
	 		* Open the interrupt pipe.
	 		*/

			softc->uhid_ipipe = usb_open_pipe(dev,epdscr);
			
			usb_endpoint_descr_t *txdscr = usb_find_cfg_descr(dev, USB_ENDPOINT_DESCRIPTOR_TYPE, (i * 4) + 1);
			//if (USB_ENDPOINT_DIR_OUT(txdscr->bEndpointAddress))
			//	printf("Opened an outgoing endpoint\n");

			softc->uhid_ipipe_tx = usb_open_pipe(dev, txdscr);

			

			softc->uhid_ipipemps = GETUSBFIELD(epdscr,wMaxPacketSize);
			softc->uhid_ipipemps_tx = GETUSBFIELD(epdscr,wMaxPacketSize);

			usbctrl_set_leds(softc, 1); //Clear leds, FIXME: Implement controller shutdown somewhere
			usbctrl_poll(softc);

			/*
			 * Queue a transfer on the interrupt endpoint to catch
			 * our first characters.
	 		*/

			usbctrl_queue_intreq(dev,softc);

			

		}

		//Make sure we set the lights for any previously detected wired controllers
		usbctrl_set_rol(controller_mask);

	}
	else
	{

	softc = KMALLOC(sizeof(usbctrl_softc_t),0);
	memset(softc,0,sizeof(usbctrl_softc_t));
	softc->dev = dev;
	dev->ud_private = softc;

	softc->is_wireless = (wireless);

	epdscr = usb_find_cfg_descr(dev,USB_ENDPOINT_DESCRIPTOR_TYPE,0);
	ifdscr = usb_find_cfg_descr(dev,USB_INTERFACE_DESCRIPTOR_TYPE,0);

	usb_endpoint_descr_t *txdscr = usb_find_cfg_descr(dev, USB_ENDPOINT_DESCRIPTOR_TYPE,1);

	
	//if (USB_ENDPOINT_DIR_OUT(txdscr->bEndpointAddress))
	//	printf("Opened an outgoing endpoint\n");

	softc->uhid_ipipe_tx = usb_open_pipe(dev, txdscr);

	if (!epdscr || !ifdscr) {
		printf("couldn't find descriptor!\n");
		return 0;
	}
	

	
	int i;
	for (i = 0; controller_mask & (1<<i); ++i);
	printf("attached controller %d\n", i);
	softc->index = i;
	setcontroller(softc, i);
	controller_mask |= 1<<i;

	usbctrl_set_rol(controller_mask);

	// TODO: detect controller type from xbox id descriptor
	if ((GETUSBFIELD(&dev->ud_devdescr, idVendor) == 0x1430 && // xplorer
		GETUSBFIELD(&dev->ud_devdescr, idProduct) == 0x4748) ||
		(GETUSBFIELD(&dev->ud_devdescr, idVendor) == 0x1bad && // rb
		GETUSBFIELD(&dev->ud_devdescr, idProduct) == 0x0002))
		set_controller_type(softc->index, CTRL_TYPE_GUITAR);
	else
		set_controller_type(softc->index, CTRL_TYPE_GAMEPAD);

	/*
	 * Allocate a DMA buffer
	 */

	softc->uhid_imsg = usb_dma_alloc(UBR_KBD_MAX);
	if (softc->uhid_imsg == NULL) {
		printf("couldn't alloc buffer\n");
		/* Could not allocate a buffer, fail. */
		return -1;
	}
	
	/*
	 * Choose the standard configuration.
	 */

	usb_set_configuration(dev,cfgdscr->bConfigurationValue);

	/*
	 * Open the interrupt pipe.
	 */

	softc->uhid_ipipe = usb_open_pipe(dev,epdscr);
	softc->uhid_ipipemps = GETUSBFIELD(epdscr,wMaxPacketSize);

	/*
	 * Queue a transfer on the interrupt endpoint to catch
	 * our first characters.
	 */
	usbctrl_set_leds(softc, 0);
	usbctrl_queue_intreq(dev,softc);


	}

	return 0;


}

/*  *********************************************************************
	*  usbctrl_detach(dev)
	*  
	*  This routine is called when the bus scanner notices that
	*  this device has been removed from the system.  We should
	*  do any cleanup that is required.  The pending requests
	*  will be cancelled automagically.
	*  
	*  Input parameters: 
	*			 dev - usb device
	*			 
	*  Return value:
	*			 0
	********************************************************************* */

static int usbctrl_detach(usbdev_t *dev)
{
	if(dev->ud_private)
	{

	usbctrl_softc_t *uhid = dev->ud_private;
	
	printf("detached controller %d\n", uhid->index);
	setcontroller(NULL, uhid->index);
	controller_mask &= ~(1<<uhid->index);

	usbctrl_set_rol(controller_mask);

	set_controller_type(uhid->index, CTRL_TYPE_NONE);

	return 0;

	}
	else
	{
		printf("Wirless module detatched, not yet handled\n");


		return 0;

	}

}

