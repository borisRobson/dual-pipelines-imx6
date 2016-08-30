#include <gst/gst.h>
#include <stdio.h>

static gboolean low_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data);
static gboolean high_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data);
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data);
static void queue_overrun_cb(GstElement *queue, gpointer user_data);
static GstPadProbeReturn queue_data_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
gboolean create_low_res_pipeline(gpointer user_data);
gboolean create_high_res_pipeline(gpointer user_data);
gboolean link_enc_to_mux(GstElement *enc, GstElement *mux, gpointer user_data);
gboolean switch_high_res_recorder(gboolean motion, gpointer user_data);

gboolean low_src_linked, high_src_linked;

GstElement *high_pipeline, *high_src, *high_dec, *high_tee;
GstElement *high_q0, *high_fsink0;
GstElement *high_q1, *high_fsink1, *high_enc, *high_mux, *high_sink;

GstElement *low_pipeline, *low_src, *low_dec, *low_convbefore, *low_mcells, *low_convafter, *low_enc, *low_mux, *low_sink;

gulong queue_probe, block_probe;
GstPad *qsrc_blockpad, *muxpad;

gboolean f_sink_connected;
int filecount;

int main(int argc, char* argv[]){
	gboolean high_created, low_created;
	GMainLoop *loop;
	GstPad *high_sink_pad, *low_sink_pad;

	if(argc<=2){
		g_printerr("rtsp uri(s) not provided\n");
		g_printerr("usage is ./motion-rec <low-res-rtsp> <high-res-rtsp>\n");
		return -1;
	}
	//initilaise filecount variable to 0
	filecount = 0;

	//initialise gstreamer
	gst_init(&argc, &argv);

	loop = g_main_loop_new(NULL,FALSE);

	//create low res elements
	//set file src and output filename if successful	
	low_created = create_low_res_pipeline(loop);
	if(low_created){
		g_object_set(low_src, "location", argv[1], NULL);
		g_object_set(low_sink, "location", "mcells.mp4", NULL);
	}

	//create high res elements
	//set file src and output filename if successful
	high_created = create_high_res_pipeline(loop);
	if(high_created){
		g_object_set(high_src, "location", argv[2], NULL);
		g_object_set(high_sink, "location", "highres.mp4", NULL);
	}

	//if either fails, exit
	if(!high_created || !low_created){
		g_printerr("could not create all pipelines\n");
		gst_element_set_state(high_pipeline, GST_STATE_NULL);
		gst_element_set_state(low_pipeline, GST_STATE_NULL);
		gst_object_unref(low_pipeline);
		gst_object_unref(high_pipeline);
		return -1;
	}

	//set high res to playing
	gst_element_set_state(high_pipeline, GST_STATE_PLAYING);

	//start main loop
	g_main_loop_run(loop);

	//once exited send EOS to all mux & sink elements
    high_sink_pad = gst_element_get_static_pad(high_mux, "video_0");
    gst_pad_send_event(high_sink_pad, gst_event_new_eos()); 
    low_sink_pad = gst_element_get_static_pad(low_mux, "video_0");
    gst_pad_send_event(low_sink_pad, gst_event_new_eos());
    
    //set null and unref
	gst_element_set_state(high_pipeline, GST_STATE_NULL);
	gst_element_set_state(low_pipeline, GST_STATE_NULL);
	gst_object_unref(low_pipeline);
	gst_object_unref(high_pipeline);

	return 0;
}

gboolean create_high_res_pipeline(gpointer user_data){
	GstPadTemplate *tee_src_template;
	GstPad *hightee_src0, *hightee_src1, *highq0_sink, *highq1_sink;
	

	GMainLoop *loop = user_data;

	//create all elements
	high_pipeline = gst_pipeline_new("high-res-pipeline");

	high_src = gst_element_factory_make("rtspsrc", "high_src");
	high_dec = gst_element_factory_make("decodebin", NULL);
	high_tee = gst_element_factory_make("tee", NULL);

	high_q0 = gst_element_factory_make("queue", NULL);
	high_fsink0 = gst_element_factory_make("fakesink", NULL);

	high_q1 = gst_element_factory_make("queue", NULL);
	high_fsink1 = gst_element_factory_make("fakesink", NULL);
	high_enc = gst_element_factory_make("avenc_mpeg4", NULL);
	//high_enc = gst_element_factory_make("vpuenc_mpeg4", NULL);
	high_mux = gst_element_factory_make("mp4mux", NULL);
	high_sink = gst_element_factory_make("filesink", NULL);

	//confirm elements created
	if(!high_pipeline || !high_src || !high_dec || !high_tee || !high_q0 || !high_fsink0 \
		|| !high_q1 || !high_fsink1 || !high_enc || !high_mux || !high_sink){
		g_printerr("could not create all high res elements\n");		
		return FALSE;
	}

	//set callback for elements with async request pads
	g_signal_connect(high_src, "pad-added", G_CALLBACK(pad_added_handler), loop);
	g_signal_connect(high_dec, "pad-added", G_CALLBACK(pad_added_handler), loop);

	//set values for queue
	g_object_set(high_q1, "max-size-buffers", 90, "max-size-time", 0, "max-size-bytes", 0, "leaky", 0, NULL);
	g_signal_connect(high_q1, "overrun", G_CALLBACK(queue_overrun_cb), loop);

	//add elements to pipeline
	gst_bin_add_many(GST_BIN(high_pipeline),high_src, high_dec, high_tee,high_q0, high_fsink0, high_q1,high_fsink1,NULL);

	//set bus callback
	gst_bus_add_watch(GST_ELEMENT_BUS(high_pipeline),high_bus_cb,loop);

	//get tee -> queue pads and link
	tee_src_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(high_tee), "src_%u");
	hightee_src0 = gst_element_request_pad(high_tee, tee_src_template, NULL, NULL);
	hightee_src1 = gst_element_request_pad(high_tee, tee_src_template, NULL, NULL);
	g_print("Recieved tee pads '%s' and '%s'\n", gst_pad_get_name(hightee_src0), gst_pad_get_name(hightee_src1));

	highq0_sink = gst_element_get_static_pad(high_q0, "sink");
	highq1_sink = gst_element_get_static_pad(high_q1, "sink");

	if(gst_pad_link(hightee_src0, highq0_sink) != GST_PAD_LINK_OK || gst_pad_link(hightee_src1,highq1_sink) != GST_PAD_LINK_OK){
		g_printerr("could not link tee\n");
		return FALSE;
	}

	//link sink elements
	if(!gst_element_link(high_q0, high_fsink0) || !gst_element_link(high_q1, high_fsink1)){
		g_printerr("could not link sinks\n");
		return FALSE;
	}

	return TRUE;
}

gboolean create_low_res_pipeline(gpointer user_data){
	GMainLoop *loop = user_data;

	//create low res elements
	low_pipeline = gst_pipeline_new("low_res_pipeline");

	low_src = gst_element_factory_make("rtspsrc", "low_src");
	low_dec = gst_element_factory_make("decodebin", NULL);
	low_convbefore = gst_element_factory_make("videoconvert", NULL);
	low_mcells = gst_element_factory_make("motioncells", NULL);
	low_convafter = gst_element_factory_make("videoconvert", NULL);
	low_enc = gst_element_factory_make("avenc_mpeg4", NULL);
	//low_enc = gst_element_factory_make("vpuenc_mpeg4", NULL);
	low_mux = gst_element_factory_make("mp4mux", NULL);
	low_sink = gst_element_factory_make("filesink", NULL);

	//confirm all created
	if(!low_pipeline || !low_src || !low_dec || !low_convbefore || !low_mcells || !low_convafter || !low_enc || !low_mux || !low_sink){
		g_printerr("could not create all low res elements\n");
		return FALSE;
	}

	//connect async pad handler
	g_signal_connect(low_src, "pad-added", G_CALLBACK(pad_added_handler),loop);
	g_signal_connect(low_dec, "pad-added", G_CALLBACK(pad_added_handler),loop);

	//add to bin
	gst_bin_add_many(GST_BIN(low_pipeline), low_src, low_dec, low_convbefore, low_mcells, low_convafter, low_enc\
		,low_mux, low_sink,  NULL);

	//link bus callback
	gst_bus_add_watch(GST_ELEMENT_BUS(low_pipeline), low_bus_cb, loop);

	//link static elements
	if(!gst_element_link_many(low_convbefore, low_mcells, low_convafter, low_enc, NULL)){
		g_printerr("could not link all static elements\n");
		return FALSE;
	}

	gboolean mux_linked = link_enc_to_mux(low_enc, low_mux, loop);
	if(!mux_linked){
		return FALSE;
	}

	if(!gst_element_link(low_mux, low_sink)){
		g_printerr("could not link low sink\n");
		return FALSE;
	}

	return TRUE;
}

gboolean link_enc_to_mux(GstElement *enc, GstElement *mux, gpointer user_data){
	GstPadTemplate *mux_sink_template;
	GstPad *mux_sink, *enc_src;
	GstCaps *caps;

	//specify caps
	caps = gst_caps_from_string("video/mpeg");

	//use template to get mux request pad
	mux_sink_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(mux), "video_%u");
	mux_sink = gst_element_request_pad(mux, mux_sink_template, NULL, caps);
	g_print("Recieved pad:'%s' from element: '%s'\n", gst_pad_get_name(mux_sink), GST_OBJECT_NAME(mux));

	//enc pad is static
	enc_src = gst_element_get_static_pad(enc, "src");

	if(gst_pad_link(enc_src,mux_sink) != GST_PAD_LINK_OK){
		g_printerr("could not link %s to %s\n", GST_OBJECT_NAME(enc), GST_OBJECT_NAME(mux));
		return FALSE;
	}

	return TRUE;
}

static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data){
	GstPadTemplate *mux_src_template;
	GstPad *mux_sink_pad, *enc_src_pad;
	GstPadLinkReturn ret;
	GstEvent *event;
	GstPad *q_pad;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;
	GstCaps *filter = NULL;
	
	//rtspsrc will send messages for audio & video streams
	//these checks will ensure no attempt to link audio to video pulgins
	if(low_src_linked && GST_OBJECT_NAME(src) == GST_OBJECT_NAME(low_src)){
		goto exit;
	}
	if(high_src_linked && GST_OBJECT_NAME(src) == GST_OBJECT_NAME(high_src)){
		goto exit;
	}	

	g_print("Recieved new pad '%s' from '%s'\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));
	//if pad is already linked, nothing to do
	if(gst_pad_is_linked(new_pad)){
		g_print("pad linked, nothing to do\n");
		goto exit;
	}

	//depending on src set appropriate filters
	if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(high_src)){
		q_pad = gst_element_get_static_pad(high_dec, "sink");
		filter = gst_caps_new_simple("application/x-rtp",
			"media", G_TYPE_STRING,"video",
			NULL);
	}
	else if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(high_dec)){
		g_print("high dec pad_added\n");
		q_pad = gst_element_get_static_pad(high_tee, "sink");					
		filter = gst_caps_new_any();
	}
	else if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(low_src)){
		q_pad = gst_element_get_static_pad(low_dec, "sink");
		filter = gst_caps_new_simple("application/x-rtp",
			"media", G_TYPE_STRING,"video",
			NULL);
	}
	else if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(low_dec)){
		g_print("low dec pad_added\n");
		q_pad = gst_element_get_static_pad(low_convbefore, "sink");		
		filter = gst_caps_new_any();
	}
		
	//filter for rtp	
	new_pad_caps = gst_pad_query_caps(new_pad, filter);	

	if(gst_caps_is_empty(new_pad_caps)){	
		gst_object_unref(q_pad);	
		return;
	}
	//g_print("caps: '%s'\n", gst_caps_to_string(new_pad_caps));

	//reconfigure pads
	event = gst_event_new_reconfigure();
	gst_pad_send_event(new_pad, event);

	//check new pad type
	new_pad_struct = gst_caps_get_structure(new_pad_caps,0);
	new_pad_type = gst_structure_get_name(new_pad_struct);	

	//attempt link
	g_print("Attempting to link source pad '%s' to sink pad '%s'\n", GST_PAD_NAME(new_pad), GST_PAD_NAME(q_pad));
	ret = gst_pad_link(new_pad, q_pad);
	if(GST_PAD_LINK_FAILED(ret)){
		g_print(" Type is '%s' but link failed\n", new_pad_type);
	}else{
		g_print("link of %s succeeded(type: %s)\n",GST_OBJECT_NAME(src),new_pad_type);
		if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(high_src)){
			high_src_linked = TRUE;			
		}
		else if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(high_dec)){								
			GstStateChangeReturn ret = gst_element_set_state(high_pipeline, GST_STATE_PLAYING);		
			if(ret == GST_STATE_CHANGE_FAILURE){
				g_printerr("could not change state\n");
			}			
		}		
		if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(low_src)){
			low_src_linked = TRUE;
		}
		
		gst_object_unref(q_pad);
	}	
	exit:
	if(new_pad_caps != NULL){
		gst_caps_unref(new_pad_caps);
	}

}

static gboolean low_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data){
	GMainLoop *loop = user_data;
	gboolean motion;
	GstPad *high_qsrc_pad;
	high_qsrc_pad = gst_element_get_static_pad(high_q1, "src");
	
	const GstStructure *msg_struct = gst_message_get_structure(msg);
	//parse bus messages
	switch(GST_MESSAGE_TYPE(msg)){
		case GST_MESSAGE_ERROR:{
			//quit on error
			GError *err = NULL;
			gchar *dbg;
			gst_message_parse_error(msg, &err, &dbg);
			gst_object_default_error(msg->src, err, dbg);
			g_clear_error(&err);
			g_free(dbg);
			g_main_loop_quit(loop);
			break;
		}
		case GST_MESSAGE_STATE_CHANGED:{
			GstState old_state, pending_state, new_state;
			gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(low_pipeline)){
				g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 				
				if(new_state == GST_STATE_PLAYING){					
					
				}
			}
			break;
		}
		case GST_MESSAGE_ELEMENT:{
			//parse for motioncells message
			if(GST_MESSAGE_SRC(msg) == GST_OBJECT(low_mcells)){				
				//check message for motion started or stopped				
				if(gst_structure_has_field(msg_struct, "motion_begin")){
					g_print("Motion Detected\n");
					motion = TRUE;										
					/*
					if(filecount < 1){
						gst_pad_remove_probe(high_qsrc_pad, queue_probe);
					}
					*/
					filecount++;
					switch_high_res_recorder(motion, loop);
				}
				else if(gst_structure_has_field(msg_struct, "motion_finished")){
					g_print("Motion Stopped\n");
					motion = FALSE;					
					switch_high_res_recorder(motion, loop);
					//queue_probe = gst_pad_add_probe(high_qsrc_pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);					
				}
			}			
			break;
		}
		default:{			
			break;
		}
	}
	return TRUE;

}

static gboolean high_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data){GMainLoop *loop = user_data;
	GstPad *highq1_src;
	GstPad *fsink;
	

	highq1_src = gst_element_get_static_pad(high_q1, "src");

	//parse bus messages
	switch(GST_MESSAGE_TYPE(msg)){
		case GST_MESSAGE_ERROR:{
			//quit on error
			GError *err = NULL;
			gchar *dbg;
			gst_message_parse_error(msg, &err, &dbg);
			gst_object_default_error(msg->src, err, dbg);
			g_clear_error(&err);
			g_free(dbg);
			g_main_loop_quit(loop);
			break;
		}
		case GST_MESSAGE_STATE_CHANGED:{
			GstState old_state, pending_state, new_state;
			gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(high_pipeline)){
				g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
				if(new_state == GST_STATE_PLAYING && filecount < 1){					
					queue_probe = gst_pad_add_probe(highq1_src, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);					
					g_print("high bus cb probe id: '%lu\n", queue_probe);
				}		
				else if(new_state == GST_STATE_PLAYING && f_sink_connected == TRUE){
					queue_probe = gst_pad_add_probe(highq1_src, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);					
					g_print("high bus cb probe id: '%lu\n", queue_probe);
				}
			}
			break;
		}
		default:{			
			break;
		}
	}
	return TRUE;

}

static void queue_overrun_cb(GstElement *queue, gpointer user_data){
	guint buffers;
	GstPad *qsrc;
	GstState state;

	gst_element_get_state(low_pipeline, &state, NULL, GST_CLOCK_TIME_NONE);

	qsrc = gst_element_get_static_pad(queue, "src");
	//g_print("q overrun\n");
	g_object_get(queue, "current-level-buffers", &buffers, NULL);
	//g_print("current level buffers: %d\n",buffers);	
	
	//buffer is filled, remove blocking probe
	//g_print("overrun cb probe id : '%lu'\n",queue_probe );
	//gst_pad_remove_probe(qsrc, queue_probe);		
	//g_print("overrun cb probe id after remove: '%lu'\n",queue_probe );
	if(state != GST_STATE_PLAYING){
		gst_element_set_state(low_pipeline, GST_STATE_PLAYING);		
	}
}

static GstPadProbeReturn queue_data_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data){
	return GST_PAD_PROBE_OK;
}

gboolean switch_high_res_recorder(gboolean motion, gpointer user_data){
	GstPad *qsrc_pad, *enc_sink;
	char filename[12];
	guint buffers;
	g_object_get(high_q1, "current-level-buffers", &buffers, NULL);
	g_print("queue buffers: %d\n", buffers);
	
	qsrc_pad = gst_element_get_static_pad(high_q1, "src");	
	
	if(motion == TRUE){
		high_enc = gst_element_factory_make("avenc_mpeg4", NULL);
		//high_enc = gst_element_factory_make("vpuenc_mpeg4", NULL);
		high_mux = gst_element_factory_make("mp4mux", NULL);
		high_sink = gst_element_factory_make("filesink", NULL);
		//set filename
		sprintf(filename, "highres%d.mp4", filecount);
		g_object_set(high_sink, "location", filename, NULL);
		gst_bin_add_many(GST_BIN(high_pipeline), high_enc,high_mux,high_sink,NULL);
		//block qsrc pad
		if(!gst_pad_is_blocking(qsrc_pad)){
			queue_probe = gst_pad_add_probe(qsrc_pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);
			g_print("switch recoder(motion == true) probe id: '%lu\n", queue_probe);
		}else{
			g_print("already blocking\n");
		}
		
		//remove old sink	
		gst_element_set_state(high_fsink1, GST_STATE_NULL);
		if(!gst_bin_remove(GST_BIN(high_pipeline), high_fsink1)){		
			g_printerr("could not unlink old sink\n");
			return FALSE;
		}		

		//unblock and set pipeline to play
		g_print("Removing probe :%lu\n", queue_probe);
		gst_pad_remove_probe(qsrc_pad, queue_probe);
		
		//link new enc -> mux
		gboolean mux_linked = link_enc_to_mux(high_enc, high_mux, user_data);
		if(!mux_linked){
			return FALSE;
		}
		//link mux -> sink
		if(!gst_element_link(high_mux,high_sink)){
			g_printerr("could not link mux to new sink\n");
			return FALSE;
		}
		// link queue -> enc
		if(!gst_element_link(high_q1, high_enc)){
			g_printerr("could not link new enc\n");
			return FALSE;
		}

		f_sink_connected = FALSE;
		
				
		gst_element_set_state(high_enc, GST_STATE_PLAYING);
		gst_element_set_state(high_mux, GST_STATE_PLAYING);
		gst_element_set_state(high_sink, GST_STATE_PLAYING);
		
	}else{
		//block qsrc pad
		if(!gst_pad_is_blocking(qsrc_pad)){
			queue_probe = gst_pad_add_probe(qsrc_pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);
			g_print("switch recoder(motion == false) probe id: '%lu\n", queue_probe);
		}else{
			g_print("already blocking\n");
		}		
		
		high_fsink1 = gst_element_factory_make("fakesink", NULL);
		//eos old elements
		enc_sink = gst_element_get_static_pad(high_enc, "sink");
		gst_pad_send_event(enc_sink, gst_event_new_eos());
		//set old element to null
		gst_element_set_state(high_enc, GST_STATE_NULL);
		gst_element_set_state(high_mux, GST_STATE_NULL);
		gst_element_set_state(high_sink, GST_STATE_NULL);
		gst_bin_remove_many(GST_BIN(high_pipeline), high_enc,high_mux,high_sink,NULL);
		//add new fsink
		gst_bin_add(GST_BIN(high_pipeline), high_fsink1);
		if(!gst_element_link(high_q1, high_fsink1)){
			g_printerr("could not link new fsink\n");
			return FALSE;
		}

		f_sink_connected = TRUE;

		//unblock and set pipeline to play
		gst_pad_remove_probe(qsrc_pad, queue_probe);		
		gst_element_set_state(high_fsink1, GST_STATE_PLAYING);			
	}
	return TRUE;
}