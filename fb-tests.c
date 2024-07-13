/* for timespec and clock_gettime */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

// see man ioctl_console
#include <linux/kd.h> /* Definition of op constants */
#include <sys/ioctl.h>

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
 *           __u32 type_aux;                 // Interleave for interleaved
 * Planes
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
 *           __u16 reserved[2];              // Reserved for future
 * compatibility
 *   };
 *
 * See also the kernel documentation
 *      https://www.kernel.org/doc/Documentation/fb/api.txt
 */

// TODO Group global state
char* fbp;
// TODO find better name for 'screensize'
long int screensize = 0; /* only the size of one buffer */
// TODO make this a commandline argument
// HERE you should adjust the buffer count:
//   1: for single buffering
//   2: for double buffering
//   3: for triple buffering
const unsigned int buffer_count = 2;
unsigned int cur_buffer_index = 0;

inline static int draw_pixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
	long long int location = 0;

	if (!(0 <= x && x < vinfo.xres && 0 <= y && y < vinfo.yres)) /* pixel out of bounds */
		return -2;

	location = cur_buffer_index * screensize;
	location += (x) * (vinfo.bits_per_pixel / 8) + (y)*finfo.line_length;

	switch (vinfo.bits_per_pixel) {
		case 32:
			*(fbp + location) = blue;
			*(fbp + location + 1) = green;
			*(fbp + location + 2) = red;
			*(fbp + location + 3) = 0; /* No transparency */
			break;
		case 16: {
			/* TODO describe which color information is dropped */
			// TODO This not correctly crops the channels. They may
			// overwrite each other!
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

void fill_screen_black(void) {
	/* faster than draw_pixel with nested for-loops */
	for (int y = 0; y < vinfo.yres; y++) {
		void* p = fbp + cur_buffer_index * screensize + y * finfo.line_length;
		size_t size = (vinfo.xres + vinfo.xoffset) * vinfo.bits_per_pixel / 8;
		memset(p, 0, size);
	}
}

int draw_solid_rect(int pos_x, int pos_y, int width, int height, uint8_t r, uint8_t g, uint8_t b) {
	int ret;
	for (int x = pos_x; x < pos_x + width; x++) {
		for (int y = pos_y; y < pos_y + height; y++) {
			ret = draw_pixel(x, y, r, g, b);
			if (ret) return ret;
		}
	}

	return 0;
}

long long int get_time_in_ms(void) {
	struct timespec spec;

	clock_gettime(CLOCK_REALTIME, &spec); /* Check for errors? */

	return (long long int)spec.tv_sec * 1000 + spec.tv_nsec / 1000 / 1000;
}

static bool exit_mainloop = false;

/* See '$ man 7 signal' for signal safe library functions */
void sighandler(int signal) {
	if (signal == SIGINT || signal == SIGTERM) {
		exit_mainloop = true;
	} else {
		const char* msg = "No special handler code for signal!\n";
		write(STDERR_FILENO, msg, strlen(msg));
	}
}

int wait_for_vsync(int fd) {
	int zero = 0, ret;

	ret = ioctl(fd, FBIO_WAITFORVSYNC, &zero);
	if (ret) {
		fprintf(stderr, "Error for FBIO_WAITFORVSYNC: ret=%i (%s)\n", ret, strerror(errno));
		return ret;
	}

	return 0;
}

int mainloop(int fd) {
	int frame_counter = 0;
	long long int last_time_reset_ms = get_time_in_ms();
	double last_fps = 0.0;

	exit_mainloop = false;

	long long int last_time = get_time_in_ms();

	while (!exit_mainloop) {
		long long int cur_time = get_time_in_ms();
		int pos = (cur_time / 10) % (vinfo.yres - 50);
		uint8_t r = (cur_time / 20 + 128) % 256;
		uint8_t g = (cur_time / 40 + 256) % 256;
		uint8_t b = (cur_time / 80 + 0) % 256;
		int ret;

		fill_screen_black();

		/*for (y = 100 + pos; y < 300 + pos; y++) {
		    for (x = 100; x < 300; x++) {
			draw_pixel(x, y, r, g, b)
		    }
		}*/
		ret = draw_solid_rect(100, pos, vinfo.xres - 200, 50, r, g, b);
		if (ret) return ret;

		//	draw_solid_rect(pos, 300, 400,400, 0, 0, 255);

		/* Wait for vsync to happend */
		ret = 0;
		wait_for_vsync(fd);
		if (ret) return ret; /* bail out on error */

		/* move scanout pointer to buffer that was rendered */
		vinfo.yoffset = vinfo.yres * cur_buffer_index;
		if (ioctl(fd, FBIOPAN_DISPLAY, &vinfo) == -1) {
			perror("Cannot FBIOPAN_DISPLAY");
			return -5;
		}

		/* Must be after the PAN operation!! */
		cur_buffer_index = (cur_buffer_index + 1) % buffer_count;

		/* Do FPS counter*/
		frame_counter++;
		if (last_time_reset_ms + 1000 <= cur_time) {
			last_fps = (double)frame_counter /
				   ((double)cur_time - (double)last_time_reset_ms) * 1000;
			// fprintf(stderr, "%.2ffps frames %i\n", last_fps, frame_counter);
			frame_counter = 0;
			last_time_reset_ms = cur_time;
		}

		last_time = cur_time;
	}

	// fprintf(stderr, "last %.2ffps\n", last_fps);

	return 0;
}

int cmd_help(void) {
	// TODO Improve help text!
	fprintf(stderr, "Commands are: 'info' or 'draw'\n");
	return 0;
}

int cmd_draw(int fb_fd) {
	int vconsole_fd;
	int ret;
	long long int mmap_size;

	// TODO Use fd '0'/STDIN and check whether it's a tty. TOOD Annd check
	// if it's tty0 or somthing. Should not be a pseudo tty!
	vconsole_fd = open("/dev/tty0", O_RDWR);
	if (vconsole_fd < 0) {
		perror("Error vconsole fd");
		// TODO make error correct
		return 4;
	}

	printf("Setting KD_GRAPHICS now!\n");

	// TODO only with this sleep the last printf statement is display
	// correctly on the screen!
	sleep(1);

	// TODO Check KDGETMODE and restore it to original value
	// TODO add argument to make this optional
	if (ioctl(vconsole_fd, KDSETMODE, KD_GRAPHICS)) {
		perror("Error in KDSETMODE KD_GRAPHICS");
		ret = 5;
		goto close_vconsole_fd;
	}

	/* Make room for double/triple buffering */
	// TODO reset the value to the previous value!
	vinfo.yres_virtual = buffer_count * vinfo.yres;

	if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo) == -1) {
		perror("Error resize virtual buffer size");
		ret = 6;
		goto cleanup_kdsetmode;
	}

	// Figure out the size of the screen in bytes
	/* TODO Must offsetss be used here? */
	screensize = vinfo.yres * finfo.line_length;
	mmap_size = vinfo.yres * finfo.line_length * buffer_count;

	// Map the device to memory
	fbp = (char*)mmap(0, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fbp == MAP_FAILED) {
		perror("Error: failed to map framebuffer device to memory");
		ret = 7;
		goto cleanup_kdsetmode;
	}

	ret = mainloop(fb_fd);

	munmap(fbp, mmap_size);

	/* FIXME clean framebuffer settings */
cleanup_kdsetmode:
	if (ioctl(vconsole_fd, KDSETMODE, KD_TEXT)) {
		perror("Error in KDSETMODE KD_TEXT");
	}
	printf("Setting KD_TEXT again\n");

close_vconsole_fd:
	close(vconsole_fd);

	return ret;
}

int cmd_info(void) {
	printf(
	    "fb_var_screeninfo {\n"
	    "\txres = %d\n"
	    "\tyres = %d\n"
	    "\txres_virtual = %d\n"
	    "\tyres_virtual = %d\n"
	    "\txoffset = %d\n"
	    "\tyoffset = %d\n"
	    "\tbits_per_pixel = %d\n"
	    "}\n",
	    vinfo.xres, vinfo.yres, vinfo.xres_virtual, vinfo.yres_virtual, vinfo.xoffset,
	    vinfo.yoffset, vinfo.bits_per_pixel);

	printf(
	    "fb_fix_screeninfo {\n"
	    "\tid = '%s'\n"
	    "\tsmem_len = %d\n"
	    "\tline_length = %d\n"
	    "\t[...]\n"
	    "}\n",
	    finfo.id, finfo.smem_len, finfo.line_length);

	// TODO Explain the padding here
	int row_in_bytes = vinfo.xres * vinfo.bits_per_pixel / 8;
	printf("xres * bits_per_pixel / 8 = %d bytes\n", row_in_bytes);
	printf("line padding = %d bytes\n", finfo.line_length - row_in_bytes);

	// TODO Explain it more here!
	float count_of_lines = finfo.smem_len / (float)finfo.line_length;
	float count_of_buffers = count_of_lines / vinfo.yres;
	printf("Count of possible buffers: %.02f\n", count_of_buffers);

	// TODO Show value of
	//    /sys/module/drm_kms_helper/parameters/drm_fbdev_overalloc
	// This controls the amount of physical memory that is allocated at
	// boot up.

	return 0;
}

typedef enum command {
	COMMAND_NONE,
	COMMAND_INFO,
	COMMAND_DRAW,
} command_t;

int main(int argc, char** argv) {
	int fb_fd;
	int ret;
	command_t command = COMMAND_NONE;

	// Simple hand-made command line parser
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "info") == 0) {
			command = COMMAND_INFO;
		} else if (strcmp(argv[i], "draw") == 0) {
			command = COMMAND_DRAW;
		} else {
			fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
			return 1;
		}
	}

	// Open the file for reading and writing
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd == -1) {
		perror("Error: cannot open framebuffer device");
		return 1;
	}

	// Get fixed screen information
	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
		perror("Error reading fixed information");
		ret = 2;
		goto close_fb_fd;
	}

	// Get variable screen information
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
		perror("Error reading variable information");
		ret = 3;
		goto close_fb_fd;
	}

	/* install signal handler for CTRL+C (SIGINT) and SIGTERM */
	/* TODO Catch errors */
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	switch (command) {
		case COMMAND_INFO:
			ret = cmd_info();
			break;
		case COMMAND_DRAW:
			ret = cmd_draw(fb_fd);
			break;
		default:
			ret = cmd_help();
			break;
	}
	if (ret) fprintf(stderr, "Mainloop ended with error %d\n", ret);

close_fb_fd:
	close(fb_fd);

	return ret;
}
