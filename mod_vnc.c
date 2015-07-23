/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2015, Seven Du <dujinfang@gmail.com>
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
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Seven Du <dujinfang@gmail.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 *
 * mod_x11 -- X11 Functions
 *
 *
 */

#include <switch.h>
#include <rfb/rfb.h>

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
					server->frameBuffer, width * 4, width, height);
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

SWITCH_MODULE_LOAD_FUNCTION(mod_vnc_load)
{
	switch_application_interface_t *app_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "vnc_video", "show video on x11", "show video on x11", vnc_video_function, "[display]", SAF_NONE);

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
