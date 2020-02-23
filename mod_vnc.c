/* 
 * FreeSWITCH Module to interact with VNC
 * Copyright (C) 2015-2020, Seven Du <dujinfang@gmail.com>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Module to interact with VNC
 *
 * The Initial Developer of the Original Code is
 * Seven Du <dujinfang@gmail.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 *
 * mod_vnc -- interact with VNC Desktop
 *
 *
 */

#include <switch.h>
#include <rfb/rfb.h>
#include <rfb/rfbclient.h>
#include <libyuv.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_vnc_load);
SWITCH_MODULE_DEFINITION(mod_vnc, mod_vnc_load, NULL, NULL);

typedef struct vnc_context_s {
	int running;
} vnc_context_t;

static void vnc_video_thread(switch_core_session_t *session, void *obj)
{
	vnc_context_t *context = obj;
	switch_codec_t *codec;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_frame_t *frame;
	uint32_t width = 0, height = 0;
	uint32_t decoded_pictures = 0;
	int count = 0;
	int argc = 0;
	char **argv = NULL;
	rfbScreenInfoPtr server = NULL;

	context->running = 1;

	if (!switch_channel_ready(channel)) {
		goto done;
	}

	codec = switch_core_session_get_video_read_codec(session);

	if (!codec) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Channel has no video read codec\n");
		goto done;
	}

	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_READ);
	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_WRITE);

	while (switch_channel_ready(channel)) {
		switch_status_t status = switch_core_session_read_video_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0);

		if (switch_channel_test_flag(channel, CF_BREAK)) {
			switch_channel_clear_flag(channel, CF_BREAK);
			break;
		}

		if (!SWITCH_READ_ACCEPTABLE(status)) {
			break;
		}

		if (!count || ++count == 101) {
			switch_core_session_request_video_refresh(session);
			count = 1;
		}
			

		if (frame && frame->datalen > 0) {
			switch_core_session_write_video_frame(session, frame, SWITCH_IO_FLAG_NONE, 0);
		} else {
			continue;
		}

		if (switch_test_flag(frame, SFF_CNG) || frame->datalen < 3) {
			continue;
		}

		if (frame->img) {
			if (frame->img->d_w > 0 && !width) {
				width = frame->img->d_w;
				switch_channel_set_variable_printf(channel, "video_width", "%d", width);
			}

			if (frame->img->d_h > 0 && !height) {
				height = frame->img->d_h;
				switch_channel_set_variable_printf(channel, "video_height", "%d", height);
			}

			decoded_pictures++;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "picture#%d %dx%d\n", decoded_pictures, frame->img->d_w, frame->img->d_h);

			if (!server) {
				server = rfbGetScreen(&argc, argv, width, height, 8, 3, 4);
				server->frameBuffer = malloc(width * height * 4);
				switch_assert(server->frameBuffer);

				rfbInitServer(server);
				rfbRunEventLoop(server, -1, TRUE);
			}

			if (server && rfbIsActive(server)) {
				I420ToABGR(frame->img->planes[SWITCH_PLANE_Y], frame->img->stride[SWITCH_PLANE_Y],
					frame->img->planes[SWITCH_PLANE_U], frame->img->stride[SWITCH_PLANE_U],
					frame->img->planes[SWITCH_PLANE_V], frame->img->stride[SWITCH_PLANE_V],
					(uint8_t *)server->frameBuffer, width * 4, width, height);
				rfbMarkRectAsModified(server,0, 0, width, height);
			}
		}
	}

 done:
	// rfbShutdownServer(server, TRUE);
	rfbShutdownServer(server, FALSE);
	free(server->frameBuffer);
	rfbScreenCleanup(server);
 	context->running = 0;
	return;
}

SWITCH_STANDARD_APP(vnc_video_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	vnc_context_t context = { 0 };
	const char *moh = switch_channel_get_hold_music(channel);
	// char *msg = "Hello FreeSWITCH";

	if (zstr(moh)) {
		moh = "silence_stream://-1";
	}

	switch_channel_answer(channel);
	switch_core_session_request_video_refresh(session);

	switch_channel_set_flag(channel, CF_VIDEO_DECODED_READ);

	switch_core_media_start_video_function(session, vnc_video_thread, &context);

	switch_ivr_play_file(session, NULL, moh, NULL);

	switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

	while (context.running) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Waiting video thread\n");
		switch_yield(1000000);
	}

	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

	switch_core_media_end_video_function(session);
	switch_core_session_video_reset(session);
}

typedef struct vncc_context_s { // client
	int running;
	rfbClient* cl;
	switch_image_t *img;
} vncc_context_t;

static rfbBool resize(rfbClient* client)
{
	int width= client->width;
	int height=client->height;
	// int depth=client->format.bitsPerPixel;
	uint8_t *data = malloc(width * height * 4);
	switch_assert(data);

	client->updateRect.x = client->updateRect.y = 0;
	client->updateRect.w = width;
	client->updateRect.h = height;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "resize: %dx%d\n", width, height);

	client->frameBuffer = data;

	return TRUE;
}

static void update(rfbClient* cl,int x,int y,int w,int h)
{
	// vncc_context_t *context = rfbClientGetClientData(cl, "__context");
	// printf("update x:%d, y:%d, w:%d, h:%d\n", x, y, w, h);
}

static char* password(rfbClient *client) {
	return strdup("12345678");
}

static void vncc_video_thread(switch_core_session_t *session, void *obj)
{
	vncc_context_t *context = obj;
	switch_codec_t *codec;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_frame_t *frame;
	switch_dtmf_t dtmf = { 0 };
	uint32_t width = 0, height = 0;
	// uint32_t decoded_pictures = 0;
	int count = 0;
	// int argc = 0;
	// char **argv = NULL;
	int ret;


	context->running = 1;

	if (!switch_channel_ready(channel)) {
		goto done;
	}

	codec = switch_core_session_get_video_read_codec(session);

	if (!codec) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Channel has no video read codec\n");
		goto done;
	}

	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_READ);
	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_WRITE);

	width = context->cl->width;
	height = context->cl->height;

	if (!width || !height) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error %dx%d\n", context->cl->width, context->cl->height);
		goto done;
	}

	context->img = switch_img_alloc(NULL, SWITCH_IMG_FMT_I420, width, height, 1);
	switch_assert(context->img);

	SendPointerEvent(context->cl, 10, 10, rfbButton1Mask);
	// SendPointerEvent(context->cl, 10, 10, 0);

	SendPointerEvent(context->cl, 100, 100, rfbButton1Mask);
	SendPointerEvent(context->cl, 100, 100, 0);

	while (switch_channel_ready(channel)) {
		switch_status_t status = switch_core_session_read_video_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0);

		if (switch_channel_test_flag(channel, CF_BREAK)) {
			switch_channel_clear_flag(channel, CF_BREAK);
			break;
		}

		if (!SWITCH_READ_ACCEPTABLE(status)) {
			break;
		}

		if (!count || ++count == 101) {
			switch_core_session_request_video_refresh(session);
			count = 1;
		}

		if (frame && frame->datalen > 0) {
			// switch_core_session_write_video_frame(session, frame, SWITCH_IO_FLAG_NONE, 0);
		} else {
			continue;
		}

		if (switch_test_flag(frame, SFF_CNG) || frame->datalen < 3) {
			continue;
		}

		if (switch_channel_has_dtmf(channel)) {
			uint32_t k = 0;

			switch_channel_dequeue_dtmf(channel, &dtmf);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "DTMF: %c 0x%x\n", dtmf.digit, dtmf.digit);

			// SendPointerEvent(context->cl, 100, 100, rfbButton1Mask);
			SendPointerEvent(context->cl, 100, 100, 0);

			switch (dtmf.digit) {
				// case 0x32: k = XK_Up; break;
				// case 0x34: k = XK_Left; break;
				// case 0x36: k = XK_Right; break;
				// case 0x38: k = XK_Down; break;


				case 0x30: k = XK_k; break;
				case 0x31: k = XK_j; break;
				case 0x32: k = XK_l; break;
				case 0x33: k = XK_p; break;
				case 0x34: k = XK_q; break;
				case 0x36: k = XK_Up; break;
				case 0x37: k = XK_Down; break;
				case 0x38: k = XK_KP_Enter; break;
				case 0x39: k = XK_KP_Space; break;

				default:
					k = dtmf.digit;
			}

			// SendKeyEvent(context->cl, k, FALSE);
			SendKeyEvent(context->cl, k, TRUE);
			SendKeyEvent(context->cl, k, FALSE);
			// SendKeyEvent(context->cl, XK_BackSpace, FALSE);
		}

		ret = WaitForMessage(context->cl, 1000);

		if (ret < 0) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error %d\n", ret);
			goto done;
		} else if (ret == 0) {
			goto img;
		}

		if(!HandleRFBServerMessage(context->cl)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error processing msg %d\n", ret);
			goto done;
		}

		img:
		if (frame->img) {
			// switch_img_free(&frame->img);
			ABGRToI420((uint8_t *)context->cl->frameBuffer, width * 4,
				context->img->planes[0], context->img->stride[0],
				context->img->planes[1], context->img->stride[1],
				context->img->planes[2], context->img->stride[2],
				width, height);
			// switch_img_copy(context->img, &frame->img);
			frame->img = context->img;
		}

		switch_core_session_write_video_frame(session, frame, SWITCH_IO_FLAG_NONE, 0);
		frame->img = NULL;
	}

 done:
	context->running = 0;
	switch_img_free(&context->img);
	return;
}

SWITCH_STANDARD_APP(vnc_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	vncc_context_t context = { 0 };
	const char *moh = switch_channel_get_hold_music(channel);
	// char *msg = "Hello FreeSWITCH";
	int argc = 2;
	char *argv[2];

	argv[0] = "freeswith";
	argv[1] = (char *)data;

	if (zstr(moh)) {
		moh = "silence_stream://-1";
	}

	context.cl = rfbGetClient(8,3,4);
	context.cl->MallocFrameBuffer = resize;
	context.cl->canHandleNewFBSize = FALSE;
	context.cl->GotFrameBufferUpdate = update;
	context.cl->listenPort = LISTEN_PORT_OFFSET;
	context.cl->GetPassword = password;
	rfbClientSetClientData(context.cl, "__context", &context);

	if(!rfbInitClient(context.cl, &argc, argv)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error connecting to VNC server\n");
		goto end;
	}

	switch_channel_answer(channel);
	switch_core_session_request_video_refresh(session);

	switch_channel_set_flag(channel, CF_VIDEO_DECODED_READ);

	switch_core_media_start_video_function(session, vncc_video_thread, &context);

	switch_ivr_play_file(session, NULL, moh, NULL);

	switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

	while (context.running) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Waiting video thread\n");
		switch_yield(1000000);
	}

end:
	if(context.cl) rfbClientCleanup(context.cl);

	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

	switch_core_media_end_video_function(session);
	switch_core_session_video_reset(session);
}

SWITCH_MODULE_LOAD_FUNCTION(mod_vnc_load)
{
	switch_application_interface_t *app_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "vnc_video", "show video on x11", "show video on x11", vnc_video_function, "[display]", SAF_NONE);
	SWITCH_ADD_APP(app_interface, "vnc", "as a vnc client", "connect to a vnc server", vnc_function, "[display]", SAF_NONE);

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
