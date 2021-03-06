/*
 * sdl-window.c - Window management with SDL
 *
 *  Routines for window creation and image rendering
 *  using the SDL cross-platform library.
 *
 *  Created on: 23 Jun 2016
 *      Author: ajuaristi <a@juaristi.eus>
 */
#include <limits.h>
#include <linux/videodev2.h>
#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"
#include "utils.h"
#include "window.h"

#define WINDOW_TITLE "Appbase CCTV (by ajuaristi)"

struct window {
	enum frame_format format;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
};

static void sdl_render_from_texture(SDL_Renderer *r, SDL_Texture *t)
{
	SDL_RenderClear(r);
	SDL_RenderCopy(r, t, NULL, NULL);
	SDL_RenderPresent(r);
}

static bool sdl_render_yuyv(struct window *w, const struct frame *f)
{
	unsigned int bpp = SDL_BYTESPERPIXEL(SDL_PIXELFORMAT_YUY2);

	/*
	 * Check to prevent read overrun, since SDL_UpdateTexture()
	 * does not take length as an argument (although we've
	 * already specified texture width and height in SDL_CreateTexture()).
	 */
	if (f->width * f->height * bpp > f->frame_bytes_used)
		return false;

	SDL_UpdateTexture(w->texture,
			NULL,
			f->frame_data,
			f->width * bpp);
	sdl_render_from_texture(w->renderer, w->texture);

	return true;
}

static bool sdl_render_jpeg(struct window *w, const char *data, size_t len)
{
	SDL_Surface *s = NULL;
	SDL_Texture *t;
	SDL_RWops *ops = SDL_RWFromConstMem(data, len);

	if (!ops)
		goto fail;

	s = IMG_LoadJPG_RW(ops);
	if (!s)
		goto fail;

	t = SDL_CreateTextureFromSurface(w->renderer, s);
	if (!t)
		goto fail;
	SDL_FreeSurface(s);

	sdl_render_from_texture(w->renderer, t);

	SDL_DestroyTexture(t);
	SDL_FreeRW(ops);
	return true;

fail:
	if (s)
		SDL_FreeSurface(s);
	if (ops)
		SDL_FreeRW(ops);
	return false;
}

static void sdl_close(struct window *w)
{
	if (w) {
		if (w->texture)
			SDL_DestroyTexture(w->texture);
		if (w->renderer)
			SDL_DestroyRenderer(w->renderer);
		if (w->window)
			SDL_DestroyWindow(w->window);
		if (w->format == FRAME_FORMAT_JPEG)
			IMG_Quit();
		SDL_Quit();
	}
}

bool window_is_closed()
{
	return SDL_QuitRequested();
}

struct window *start_window(size_t width, size_t height, enum frame_format format)
{
	struct window *w = ec_malloc(sizeof(struct window));

	/*
	 * SDL_CreateWindow() expects signed int values for width and height
	 * whereas we're using unsigned size_t values throughout the code.
	 */
	if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX || !FRAME_FORMAT_IS_SUPPORTED(format))
		goto fail;
	w->format = format;

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		goto fail;
	if (format == FRAME_FORMAT_JPEG &&
			((IMG_Init(IMG_INIT_JPG) & IMG_INIT_JPG) != IMG_INIT_JPG))
		goto fail;

	w->window = SDL_CreateWindow(WINDOW_TITLE,
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			(int) width,
			(int) height,
			SDL_WINDOW_SHOWN);
	if (!w->window)
		goto fail_uninitialize;

	w->renderer = SDL_CreateRenderer(w->window, -1, SDL_RENDERER_ACCELERATED);
	if (!w->renderer)
		goto fail_uninitialize;

	w->texture = NULL;
	if (format == FRAME_FORMAT_YUYV) {
		w->texture = SDL_CreateTexture(w->renderer,
				SDL_PIXELFORMAT_YUY2,
				SDL_TEXTUREACCESS_STREAMING,
				(int) width,
				(int) height);
		if (!w->texture)
			goto fail_uninitialize;
	}

	return w;

fail_uninitialize:
	sdl_close(w);

fail:
	free(w);
	return NULL;
}

bool window_render_frame(struct window *w, struct frame *f)
{
	bool result = false;

	if (!w || !f || !f->frame_data || !f->frame_bytes_used)
		goto end;

	switch (w->format) {
	case FRAME_FORMAT_YUYV:
		result = sdl_render_yuyv(w, f);
		break;
	case FRAME_FORMAT_JPEG:
		result = sdl_render_jpeg(w, (const char *) f->frame_data, f->frame_bytes_used);
		break;
	default:
		break;
	}

end:
	return result;
}

void destroy_window(struct window *w)
{
	if (w) {
		sdl_close(w);
		free(w);
	}
}
