#include <input/input.h>

static struct controller_data_s ctrl[4];
static int valid[4];
static int ctrl_type[4];

int get_controller_data(struct controller_data_s *d, int port)
{
	if (port >= 4)
		return 0;
	*d = ctrl[port];
	int r = valid[port];
	valid[port] = 0;
	return r;
}

void set_controller_data(int port, const struct controller_data_s *d)
{
	if (port >= 4)
		return;
	ctrl[port] = *d;
	valid[port] = 1;
}

int get_controller_type(int port)
{
	if (port >= 4)
		return 0;
	return ctrl_type[port];
}

void set_controller_type(int port, int type)
{
	if (port >= 4)
		return;
	ctrl_type[port] = type;
}

extern int usbctrl_set_rumble(int port, uint8_t l, uint8_t r);

void set_controller_rumble(int port, uint8_t l, uint8_t r)
{
	usbctrl_set_rumble(port, l, r);
}
