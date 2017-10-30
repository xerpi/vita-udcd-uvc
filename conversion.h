#ifndef CONVERSION_H
#define CONVERSION_H

void rgb888_to_yuy2(const unsigned char *rgb, int rgb_pitch, unsigned char *yuy2,
		    int yuy2_pitch, int width, int height);

void rgba8888_to_yuy2(const unsigned char *rgba, int rgba_pitch, unsigned char *yuy2,
		     int yuy2_pitch, int width, int height);


#endif
