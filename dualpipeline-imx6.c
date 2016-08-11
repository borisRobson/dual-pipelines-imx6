#include <gst/gst.h>
#include <stdio.h>

static void pad_added_handler(GstElement *el_src, GstPad *new_pad, gpointer user_data);
static gboolean low_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data);
static gboolean high_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data);
static GstPadProbeReturn queue_data_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

GstElement *lowsrc, *highsrc;
GstElement *lowdepay, *highdepay;
GstElement *lowrespipe, *highrespipe;
GstElement *lowmcells;
GstElement *curr_sink;
GstElement *q3;
GstElement *highmux;
GstElement *highenc;
gulong queue_probe;
GstPad *qpad, *muxpad, *encpad;

GstPad *tee_video2_pad, *tee_video_pad;
GstPad *queue_video_pad, *queue_video2_pad;

int motioncount, filecount;

int main(int argc, char* argv[]){
	GMainLoop *loop;	
	GstElement *lowparse, *lowdec, *lowconvbefore, *lowconvafter,   *lowfsink;
	GstElement *highparse, *highdec, *highsink, *highfsink;
	GstElement *highfilter;
	GstElement *q1, *tee, *q2;
	GstPadTemplate *tee_src_template, *mux_src_template;

	//check input parameters given
	if(argc <=2){
		g_printerr("rtsp addresses not given\n");
		g_printerr("usage is ./dualpipeline <low-res-rtsp> <high-res-rtsp>\n");
		g_printerr("include authentication if neccessary\n");
		return -1;
	}

	//init gstreamer
	gst_init(&argc, &argv);

	//create elements
	loop = g_main_loop_new(NULL, FALSE);

	lowrespipe = gst_pipeline_new("low-res-pipeline");
	highrespipe = gst_pipeline_new("high-res-pipeline");

	lowsrc = gst_element_factory_make("rtspsrc", "lowsrc");
	lowdepay = gst_element_factory_make("rtph264depay", NULL);
	lowparse = gst_element_factory_make("h264parse", NULL);
	//lowdec = gst_element_factory_make("avdec_h264", NULL);
	lowdec = gst_element_factory_make("vpudec", NULL);
	lowconvbefore = gst_element_factory_make("videoconvert", NULL);
	lowmcells = gst_element_factory_make("motioncells", "mcells");
	lowconvafter = gst_element_factory_make("videoconvert", NULL);	
	lowfsink = gst_element_factory_make("fakesink", "lowfsink");

	highsrc	= gst_element_factory_make("rtspsrc", "highsrc");
	highdepay = gst_element_factory_make("rtph264depay", NULL);
	highparse = gst_element_factory_make("h264parse", NULL);
	//highdec = gst_element_factory_make("avdec_h264", NULL);	
	highdec = gst_element_factory_make("vpudec", NULL);	
	//highenc = gst_element_factory_make("avenc_mpeg4", NULL);
	highenc = gst_element_factory_make("vpuenc_mpeg4", NULL);
	highmux = gst_element_factory_make("mp4mux", NULL);
	highsink = gst_element_factory_make("fakesink", NULL);
	highfsink = gst_element_factory_make("fakesink", NULL);

	q1 = gst_element_factory_make("queue", "q1");
	q2 = gst_element_factory_make("queue", "q2");
	q3 = gst_element_factory_make("queue", "q3");
	tee = gst_element_factory_make("tee", NULL);

	/*
		rtspsrc cannot be linked straight away, connect to pad-added-handler
		which will negotiate and link pads
	*/

	g_signal_connect(lowsrc, "pad-added", G_CALLBACK(pad_added_handler), loop);
	g_signal_connect(highsrc, "pad-added", G_CALLBACK(pad_added_handler), loop);
	g_object_set(lowsrc, "location", argv[1],  NULL);
	g_object_set(highsrc, "location", argv[2], NULL);

    //set filter caps
    highfilter = gst_element_factory_make("capsfilter", NULL);
    gst_util_set_object_arg(G_OBJECT(highfilter), "caps",
    	"video/x-raw,format=I420, framerate, GST_TYPE_FRACTION, 5, 1");

    //add elements to bins and link all but src
    gst_bin_add_many(GST_BIN(lowrespipe), lowsrc, lowdepay, lowparse, lowdec, lowconvbefore, lowmcells, lowconvafter, lowfsink, NULL);
    gst_bin_add_many(GST_BIN(highrespipe), highsrc, highdepay, highparse, highdec,q1, tee, q2, highfilter, highenc, highmux, highsink,q3, highfsink, NULL);

    gst_element_link_many(lowdepay, lowparse, lowdec, lowconvbefore, lowmcells, lowconvafter,  lowfsink, NULL);

    //link up to tee
    gst_element_link_many(highdepay, highparse, highdec,q1, tee, NULL);

    //tee1
    gst_element_link_many(q2, highfilter, highenc, NULL);
    //tee2
    gst_element_link_many(q3, highfsink, NULL);

    //assign current sink
    curr_sink = highsink;

    //negotiate mux pads and link
    mux_src_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(highmux), "video_%u");
    muxpad = gst_element_request_pad(highmux, mux_src_template, NULL, NULL);
    g_print("recieved mux pad: '%s'\n", gst_pad_get_name(muxpad));
    encpad = gst_element_get_static_pad(highenc, "src");
    if(gst_pad_link(encpad, muxpad) != GST_PAD_LINK_OK){
    	g_printerr("Could not link mux\n");
    	gst_object_unref(highrespipe);
    	gst_object_unref(lowrespipe);
    	return -1;
    }else{
    	gst_element_link(highmux, curr_sink);
    }

    //negotiate tee -> queue pads and link
    //get tee src pads
    tee_src_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(tee), "src_%u");
    tee_video_pad = gst_element_request_pad(tee, tee_src_template, NULL, NULL);
    tee_video2_pad = gst_element_request_pad(tee, tee_src_template, NULL, NULL);
    g_print("Recieved tee pads: '%s' and '%s'\n", gst_pad_get_name(tee_video_pad), gst_pad_get_name(tee_video2_pad));

    //get queue pads
    queue_video_pad = gst_element_get_static_pad(q2, "sink");
    queue_video2_pad = gst_element_get_static_pad(q3, "sink");
    g_print("recieved queue pads: '%s' , and '%s'\n", gst_pad_get_name(queue_video_pad), gst_pad_get_name(queue_video2_pad));

    //link
    if(gst_pad_link(tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK ||
    	gst_pad_link(tee_video2_pad, queue_video2_pad) != GST_PAD_LINK_OK){
    	g_printerr("could not link tee \n");
    	gst_object_unref(highrespipe);
    	gst_object_unref(lowrespipe);
    	return -1;
    }

    //set qpad
    qpad = gst_element_get_static_pad(q2, "src");

    //start playing
    gst_element_set_state(lowrespipe, GST_STATE_PLAYING);
    gst_element_set_state(highrespipe, GST_STATE_PLAYING);

    //init motioncount variable
    motioncount = filecount = 0;

    gst_bus_add_watch(GST_ELEMENT_BUS(lowrespipe), low_bus_cb, loop);
    gst_bus_add_watch(GST_ELEMENT_BUS(highrespipe), high_bus_cb, loop);

    g_main_loop_run(loop);

    //once loop exited, dispose of pipelines
    gst_element_set_state(lowrespipe, GST_STATE_NULL);
    gst_element_set_state(highrespipe, GST_STATE_NULL);

    gst_object_unref(lowrespipe);
    gst_object_unref(highrespipe);

    return 0;
}

static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data){
  GstPadLinkReturn ret;
  GstEvent *event;
  GstPad *depay_pad;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;
  GstCaps *filter = NULL;

  g_print("Received new pad '%s' from '%s'\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));
  //if pad already linked, nothing to do
  if(gst_pad_is_linked(new_pad)){
    g_print(" pad linked, ignoring...\n");
    goto exit;
  }

  //get the correct depay pad
  if(GST_OBJECT_NAME(src) == GST_OBJECT_NAME(lowsrc)){
  	depay_pad =  gst_element_get_static_pad(lowdepay, "sink");  
  }else{
  	depay_pad =  gst_element_get_static_pad(highdepay, "sink");  
  }  

  filter = gst_caps_from_string("application/x-rtp");
  new_pad_caps = gst_pad_query_caps(new_pad, filter);

  //send reconfigure event
  event = gst_event_new_reconfigure();
  gst_pad_send_event(new_pad, event);

  //check new pad type
  new_pad_struct = gst_caps_get_structure(new_pad_caps,0);
  new_pad_type = gst_structure_get_name(new_pad_struct);
  if(!g_str_has_prefix(new_pad_type, "application/x-rtp")){
    g_print(" Type: '%s', looking for rtp. Ignoring\n",new_pad_type);
    goto exit;
  }

  //attempt to link
  g_print("Attempting to link source pad '%s' to sink pad '%s'\n",GST_PAD_NAME(new_pad), GST_PAD_NAME(depay_pad));
  ret = gst_pad_link(new_pad, depay_pad);
  if(GST_PAD_LINK_FAILED(ret)){
    g_print(" Type is: '%s' but link failed.\n", new_pad_type);
  }else{
    g_print(" Link Succeeded (type: '%s')\n", new_pad_type);       
  } 

  exit:
    //unref new pad caps if required
    if(new_pad_caps != NULL){
      gst_caps_unref(new_pad_caps);
    }
    //unref depay pad
    gst_object_unref(depay_pad);    
}

static gboolean low_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data){
	GMainLoop *loop = user_data;
	char name[12];

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
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(lowrespipe)){
				g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
			}			
			break;
		}
		default:{
			//check for motioncells messages
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(lowmcells)){
				motioncount++;
				if(motioncount % 2 == 1){
					/*
						Motion detected, create new sink
						link and unblock to allow data flow						
					*/	
					filecount++;					
					g_print("Motion Detected\n");
					gst_pad_remove_probe(qpad, queue_probe);
					gst_bin_remove(GST_BIN(highrespipe), curr_sink);
					gst_bin_remove(GST_BIN(highrespipe), highmux);
					sprintf(name, "/nvdata/tftpboot/file%d.mp4", filecount);
					curr_sink = gst_element_factory_make("filesink", NULL);
					highmux = gst_element_factory_make("mp4mux", NULL);
					g_object_set(curr_sink, "location", name, NULL);
					gst_bin_add_many(GST_BIN(highrespipe),highmux, curr_sink, NULL);
					g_print("linking new sink\n");
					if(!gst_element_link_many( highenc, highmux, curr_sink, NULL)){
						g_printerr("Could not link new sink\n");
						g_main_loop_quit(loop);
						break;
					}
					gst_element_set_state(highmux, GST_STATE_PLAYING);
					gst_element_set_state(curr_sink, GST_STATE_PLAYING);
				}else{
					//Motin stopped, block data flow
					g_print("Motion Stopped\n");
					queue_probe = gst_pad_add_probe(qpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);
				}
			}
			break;
		}
	}
	return TRUE;
}

static gboolean high_bus_cb (GstBus *bus, GstMessage *msg, gpointer user_data){
	GMainLoop *loop = user_data;

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
			if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(highrespipe)){
				g_print("'%s' state changed from %s to %s. \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state)); 
				if(new_state == GST_STATE_PLAYING && motioncount == 0){
					g_print("blocking sink\n");
					queue_probe = gst_pad_add_probe(qpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, queue_data_probe_cb, user_data, NULL);
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

static GstPadProbeReturn queue_data_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data){
	GstState state;

	//get state of current sink
	gst_element_get_state(curr_sink, &state, NULL, GST_CLOCK_TIME_NONE);
	if(state == GST_STATE_PLAYING){
		muxpad = gst_element_get_static_pad(highmux, "video_0");
		//send eos to element about to be removed & set to null
		gst_pad_send_event(muxpad, gst_event_new_eos());
		gst_element_set_state(highmux, GST_STATE_NULL);
		gst_element_set_state(curr_sink, GST_STATE_NULL);
	}
	//drop the data
	return GST_PAD_PROBE_DROP;
}

