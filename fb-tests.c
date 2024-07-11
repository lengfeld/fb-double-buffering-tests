/* for timespec and clock_gettime */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

/*
 * See file /usr/include/linux/fb.h:
 *
 *   struct fb_var_screeninfo {
 *           __u32 xres;                     // visible resolution
 *           __u32 yres;
 *           __u32 xres_virtual;             // virtual resolution
 *           __u32 yres_virtual;
 *           __u32 xoffset;                  // offset from virtual to visible
 *           __u32 yoffset;                  // resolution
 *
 *           __u32 bits_per_pixel;           // guess what
 *           ...
 *   };
 *
 *   struct fb_fix_screeninfo {
 *           char id[16];                    // identification string eg "TT Builtin"
 *           unsigned long smem_start;       // Start of frame buffer mem
 *                                           // (physical address)
 *           __u32 smem_len;                 // Length of frame buffer mem
 *           __u32 type;                     // see FB_TYPE_*
 *           __u32 type_aux;                 // Interleave for interleaved Planes
 *           __u32 visual;                   // see FB_VISUAL_*
 *           __u16 xpanstep;                 // zero if no hardware panning
 *           __u16 ypanstep;                 // zero if no hardware panning
 *           __u16 ywrapstep;                // zero if no hardware ywrap
 *           __u32 line_length;              // length of a line in bytes
 *           unsigned long mmio_start;       // Start of Memory Mapped I/O
 *                                           // (physical address)
 *           __u32 mmio_len;                 // Length of Memory Mapped I/O
 *           __u32 accel;                    // Indicate to driver which
 *                                           //  specific chip/card we have
 *           __u16 capabilities;             // see FB_CAP_*
 *           __u16 reserved[2];              // Reserved for future compatibility
 *   };
 *
 * See also the kernel documentation 
 *      https://www.kernel.org/doc/Documentation/fb/api.txt
 */

char *fbp;
long int screensize = 0; /* only the size of one buffer */
int fd;
const unsigned int buffer_count = 2;
unsigned int cur_buffer_index = 0;


inline static int draw_pixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
	long long int location = 0;

	if (!(0 <= x && x < vinfo.xres && 0 <= y && y < vinfo.yres))
		/* pixel out of bounds */
		return -2;

	location = cur_buffer_index * screensize;
	location += (x) * (vinfo.bits_per_pixel/8) + (y) * finfo.line_length;

	switch(vinfo.bits_per_pixel) {
	case  32:
		*(fbp + location) = blue;
		*(fbp + location + 1) = green;
		*(fbp + location + 2) = red;
		*(fbp + location + 3) = 0;	 /* No transparency */
		break;
	case 16: {
		/* TODO describe which color information is dropped */
		unsigned short int t = red << 11 | green << 5 | blue;
		*((unsigned short int*)(fbp + location)) = t;
		break;
	}
	default:
		fprintf(stderr, "Pixel depth %d not supported\n", vinfo.bits_per_pixel);
		return -1;
	}

	return 0;
}

void fill_screen_black(void)
{
	/* faster: */
	for (int y = 0; y < vinfo.yres; y++)
		memset(fbp + cur_buffer_index * screensize + y * finfo.line_length,
			0,
			(vinfo.xres + vinfo.xoffset) * vinfo.bits_per_pixel/8);
}

int draw_solid_rect(int pos_x, int pos_y, int width, int height, uint8_t r, uint8_t g, uint8_t b)
{
	int ret; 
	for (int x = pos_x; x < pos_x + width; x++) {
		for (int y = pos_y; y < pos_y + height; y++) {
			ret = draw_pixel(x, y, r, g, b);
			if (ret)
				return ret;
		}
	}

	return 0;
}

long long int get_time_in_ms(void)
{
	struct timespec spec;

	clock_gettime(CLOCK_REALTIME, &spec); /* Check for errors? */

	return (long long int) spec.tv_sec * 1000 + spec.tv_nsec / 1000 / 1000;
}

static bool exit_mainloop = false;

/* See '$ man 7 signal' for signal safe library functions */
void sighandler(int signal)
{
	if (signal == SIGINT || signal == SIGTERM) {
		exit_mainloop = true;
	} else {
		const char* msg = "No special handler code for signal!\n";
		write(STDERR_FILENO, msg, strlen(msg));
	}
}

int wait_for_vsync(void)
{
	int zero = 0, ret;

	ret = ioctl(fd, FBIO_WAITFORVSYNC, &zero);
	if (ret) {
		fprintf(stderr, "Error for FBIO_WAITFORVSYNC: ret=%i (%s)\n", ret, strerror(errno));
		return ret;
	}

	return 0;
}

int mainloop(void)
{
	int frame_counter = 0;
	long long int last_time_reset_ms = get_time_in_ms();
	double last_fps = 0.0;

	exit_mainloop = false;

	long long int last_time = get_time_in_ms();

	while (!exit_mainloop) {
		long long int cur_time = get_time_in_ms();
		int pos = (cur_time / 10) % vinfo.yres;
		uint8_t r = (cur_time / 20 + 128) % 256;
		uint8_t g = (cur_time / 40 + 256) % 256;
		uint8_t b = (cur_time / 80 +   0) % 256;
		int ret;

		fill_screen_black();

		/*for (y = 100 + pos; y < 300 + pos; y++) {
			for (x = 100; x < 300; x++) {
				draw_pixel(x, y, r, g, b)
			}
		}*/
		ret = draw_solid_rect(200, pos, vinfo.xres - 200, 50, r, g, b);
		if (ret)
			return ret;

	//	draw_solid_rect(pos, 300, 400,400, 0, 0, 255);

		/* Wait for vsync to happend */
		ret = 0; // wait_for_vsync();
		if (ret)
			return ret; /* bail out on error */

		/* move scanout pointer to buffer that was rendered */
		vinfo.yoffset = vinfo.yres * cur_buffer_index;
		if (ioctl(fd, FBIOPAN_DISPLAY, &vinfo) == -1) {
			perror("Cannot FBIOPAN_DISPLAY");
			return -4;
		}
		/* Must be after the PAN operation!! */
		cur_buffer_index = (cur_buffer_index + 1) % buffer_count;

		/* Do fps counter*/
		frame_counter++;
		if (last_time_reset_ms + 1000 <= cur_time) {
			last_fps = (double) frame_counter / ((double) cur_time - (double) last_time_reset_ms) * 1000;
			fprintf(stderr, "%.2ffps frames %i\n",
				last_fps,
				frame_counter);
			frame_counter = 0;
			last_time_reset_ms = cur_time;
		}

		last_time = cur_time;
	}

	fprintf(stderr, "last %.2ffps", last_fps);

	return 0;
}

int main(int argc, char** argv)
{
	long long int mmap_size;
	int ret = 0;

	// Open the file for reading and writing
	fd= open("/dev/fb0", O_RDWR);
	if (fd == -1) {
		perror("Error: cannot open framebuffer device");
		return 1;
	}
	printf("The framebuffer device was opened successfully.\n");

	// Get fixed screen information
	if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
		perror("Error reading fixed information");
		return 2;
	}

	// Get variable screen information
	if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
		perror("Error reading variable information");
		return 3;
	}

	printf("physical: %dx%d, %dbpp offsets %d in x %d in y\n",
			vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
			vinfo.xoffset, vinfo.yoffset);
	printf("line_length in bytes: %d\n", finfo.line_length);
	printf("vritual: %dx%d\n", vinfo.xres_virtual, vinfo.yres_virtual);

	/* Make room for double/triple buffering */
	/* FIXME You cannot change yres_virtual. Must be set via kernel parameter.
	 * And it cannot be smaller than the kernel buffer
	 */
	printf("buffer_count %d yres %d yres_virtual %d\n", buffer_count, vinfo.yres, vinfo.yres_virtual);
	vinfo.yres_virtual = buffer_count * vinfo.yres;
	printf("buffer_count %d yres %d yres_virtual %d\n", buffer_count, vinfo.yres, vinfo.yres_virtual);
	if (ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo) == -1) {
		perror("Error resize virtual buffer size");
		ret = 4;
		goto cleanup;
	}

	printf("physical: %dx%d, %dbpp offsets %d in x %d in y\n",
			vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
			vinfo.xoffset, vinfo.yoffset);
	printf("line_length in bytes: %d\n", finfo.line_length);
	printf("vritual: %dx%d\n", vinfo.xres_virtual, vinfo.yres_virtual);

	// Figure out the size of the screen in bytes
	/* TODO Must offsetss be used here? */
	screensize = vinfo.yres * finfo.line_length;
	mmap_size = vinfo.yres * finfo.line_length * buffer_count;

	// Map the device to memory
	fbp = (char *)mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
					   fd, 0);
	if (fbp == MAP_FAILED) {
		perror("Error: failed to map framebuffer device to memory");
		return 4;
	}
	printf("The framebuffer device was mapped to memory successfully.\n");

	/* install signal handler for CTRL+C (SIGINT) and SIGTERM */
	/* TODO Catch errors */
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	ret = mainloop();
	if (ret)
		fprintf(stderr, "Mainloop ended with error %d", ret);

	munmap(fbp, mmap_size);
cleanup:
	close(fd);

	/* FIXME clean framebuffer settings */

	return ret;
}
