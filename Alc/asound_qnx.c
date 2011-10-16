/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <sys/asoundlib.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

typedef struct {
    snd_pcm_t 		*pcmHandle;

    ALvoid 			*buffer;
    ALsizei 		 size;

    ALboolean 		 doCapture;
    RingBuffer 		*ring;

    volatile int 	 killNow;
    ALvoid 			*thread;
} qnx_data;

typedef struct {
    ALCchar *name;
    int card, dev;
} DevMap;

static const ALCchar qnxDevice[] = "QNX Default";
static DevMap *allDevNameMap;
static ALuint numDevNames;
static DevMap *allCaptureDevNameMap;
static ALuint numCaptureDevNames;

#define MAX_FRAG_SIZE 4096
#define DEFAULT_RATE 48000

static DevMap *deviceList(int type, ALuint *count)
{
	snd_ctl_t 				*handle;
	snd_pcm_info_t 			 pcminfo;
	snd_pcm_channel_info_t   chninfo;
	int     				 max_cards, card, err, dev, num_devices, idx;
    DevMap 					*dev_list;
    char 				 	 name[1024];
	struct snd_ctl_hw_info 	 info;
    const char 				*cname, *dname;
    void 					*temp;

	idx = 0;
	num_devices = 0;
	max_cards = snd_cards();

    if (max_cards <= 0)
    {
        AL_PRINT("Failed to find a card.\n");
    	return 0;
    }

    dev_list = malloc(sizeof(DevMap) * 1);
    dev_list[0].name = strdup("QNX Default");
    num_devices = 1;

	for (card = 0; card < max_cards; card++)
	{
		if ((err = snd_ctl_open(&handle, card)) < 0)
		{
			continue;
		}
		if ((err = snd_ctl_hw_info(handle, &info)) < 0)
		{
			AL_PRINT("control hardware info (%i): %s\n", card, snd_strerror (err));
			snd_ctl_close(handle);
			continue;
		}

		for (dev = 0; dev < info.pcmdevs; dev++)
		{
			if ((err = snd_ctl_pcm_info(handle, dev, &pcminfo)) < 0)
			{
//				if (err != -ENOENT)
//					AL_PRINT("control digital audio info (%i): %s\n", card, snd_strerror(err));
				continue;
			}
#ifdef VERBOSE
			fprintf(stderr, "%s: %i [%s] / #%i: %s\n", info.name, card + 1, info.id, dev, pcminfo.name);
			fprintf(stderr, "  Directions: %s%s%s\n",
												pcminfo.flags & SND_PCM_INFO_PLAYBACK ? "playback " : "",
												pcminfo.flags & SND_PCM_INFO_CAPTURE ? "capture " : "",
												pcminfo.flags & SND_PCM_INFO_DUPLEX ? "duplex " : "");
			fprintf(stderr, "  Playback subdevices: %i\n", pcminfo.playback + 1);
			fprintf(stderr, "  Capture subdevices: %i\n", pcminfo.capture + 1);

			if (pcminfo.flags & SND_PCM_INFO_PLAYBACK)
			{
				for (idx = 0; idx <= pcminfo.playback; idx++)
				{
					memset (&chninfo, 0, sizeof (chninfo));
					chninfo.channel = SND_PCM_CHANNEL_PLAYBACK;
					if ((err = snd_ctl_pcm_channel_info (handle, dev, SND_PCM_CHANNEL_PLAYBACK, idx, &chninfo)) < 0)
					{
						fprintf (stderr, "Error: control digital audio playback info (%i): %s\n", card, snd_strerror (err));
					}
					else
					{
						fprintf (stderr, "  Playback subdevice #%i: %s\n", idx, chninfo.subname);
					}
				}
			}
			if (pcminfo.flags & SND_PCM_INFO_CAPTURE)
			{
				for (idx = 0; idx <= pcminfo.capture; idx++)
				{
					memset (&chninfo, 0, sizeof (chninfo));
					chninfo.channel = SND_PCM_CHANNEL_CAPTURE;
					if ((err = snd_ctl_pcm_channel_info (handle, dev, SND_PCM_CHANNEL_CAPTURE, 0, &chninfo)) < 0)
					{
						fprintf (stderr, "Error: control digital audio capture info (%i): %s\n", card, snd_strerror (err));
					}
					else
					{
						fprintf (stderr, "  Capture subdevice #%i: %s\n", idx, chninfo.subname);
					}
				}
			}
#endif

			if ((type == SND_PCM_CHANNEL_PLAYBACK && (pcminfo.flags & SND_PCM_INFO_PLAYBACK)) ||
				(type == SND_PCM_CHANNEL_CAPTURE && (pcminfo.flags & SND_PCM_INFO_CAPTURE)))
			{
				// add to the device list
				temp = realloc(dev_list, sizeof(DevMap) * (num_devices + 1));
				if (temp)
				{
					dev_list = temp;
					cname = info.name;
					dname = pcminfo.name;
					snprintf(name, sizeof(name), "%s [%s] (hw:%d,%d) via QNX", cname, dname, card, dev);
					dev_list[num_devices].name = strdup(name);
					dev_list[num_devices].card = card;
					dev_list[num_devices].dev  = dev;
					num_devices++;
				}
			}
		}
		snd_ctl_close (handle);
	}

    *count = num_devices;
    return dev_list;
}

static ALuint QNXProc(ALvoid *ptr)
{
    ALCdevice *device = (ALCdevice*)ptr;
    qnx_data  *data   = (qnx_data*)device->ExtraData;
    char 	  *write_ptr;
    int 	   avail;
    snd_pcm_channel_status_t status;

    SetRTPriority();

    ALint frame_size = FrameSizeFromDevFmt(device->FmtChans, device->FmtType);

    while (!data->killNow)
    {
    	ALint len = data->size;
        write_ptr = data->buffer;

        avail = len / frame_size;
        aluMixData(device, write_ptr, avail);

        while (len > 0 && !data->killNow)
        {
        	int wrote = snd_pcm_plugin_write(data->pcmHandle, write_ptr, len);

        	if (wrote <= 0)
        	{
        		if (wrote == -EAGAIN )
        			continue;

        	    memset(&status, 0, sizeof (status));
        	    status.channel = SND_PCM_CHANNEL_PLAYBACK;

        	    int rtn = snd_pcm_plugin_status(data->pcmHandle, &status);

        	    // we need to reinitialize the sound channel if we've underrun the buffer
        		if (status.status == SND_PCM_STATUS_UNDERRUN)
        		{
        			if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_PLAYBACK)) < 0)
        			{
        	            AL_PRINT("Invalid state detected for playback: %s\n", snd_strerror(rtn));
        	            aluHandleDisconnect(device);
        				break;
        			}
        		}
        	}
        	else
        	{
        		write_ptr += wrote;
        		len -= wrote;
        	}
        }
    }

    return 0;
}

static ALCboolean qnx_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    qnx_data *data;
    char driver[64];
    int i = -1;
    int card, dev;

    strncpy(driver, GetConfigValue("qnx", "device", "default"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;

    data = (qnx_data*)calloc(1, sizeof(qnx_data));

    if (!deviceName || strcmp(deviceName, qnxDevice) == 0)
    {
    	if (!deviceName)
    		deviceName = qnxDevice;

        card = 0;
        dev = 0;
        i = snd_pcm_open_preferred(&data->pcmHandle, &card, &dev, SND_PCM_OPEN_PLAYBACK);
    }
    else
    {
		size_t idx;

		if (!allDevNameMap)
			allDevNameMap = deviceList(SND_PCM_CHANNEL_PLAYBACK, &numDevNames);

		for (idx = 0; idx < numDevNames; idx++)
		{
			if (allDevNameMap[idx].name && strcmp(deviceName, allDevNameMap[idx].name) == 0)
			{
				if (idx > 0)
					sprintf(driver, "hw:%d,%d", allDevNameMap[idx].card, allDevNameMap[idx].dev);
				break;
			}
		}
		if (idx == numDevNames)
			return ALC_FALSE;

		i = snd_pcm_open_name(&data->pcmHandle, deviceName, SND_PCM_OPEN_PLAYBACK);
    }

    if (i < 0)
    {
		free(data);
		AL_PRINT("Could not open playback device '%s': %s\n", driver, snd_strerror(i));
		return ALC_FALSE;
	}

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = data;
    return ALC_TRUE;
}

static void qnx_close_playback(ALCdevice *device)
{
    qnx_data *data = (qnx_data*)device->ExtraData;

    snd_pcm_close(data->pcmHandle);
    free(data);
    device->ExtraData = NULL;
}

static ALCboolean qnx_reset_playback(ALCdevice *device)
{
    qnx_data 				   *data = (qnx_data*)device->ExtraData;
	snd_pcm_channel_info_t 		pi;
    snd_pcm_channel_params_t 	pp;
    snd_pcm_channel_setup_t 	setup;
    int 						format;
    int 						rtn;

    format = -1;
    switch(device->FmtType)
    {
        case DevFmtByte:
            format = SND_PCM_SFMT_S8;
            break;
        case DevFmtUByte:
            format = SND_PCM_SFMT_U8;
            break;
        case DevFmtShort:
            format = SND_PCM_SFMT_S16_LE;
            break;
        case DevFmtUShort:
            format = SND_PCM_SFMT_U16_LE;
            break;
        case DevFmtFloat:
            format = SND_PCM_SFMT_FLOAT_LE;
            break;
    }

    memset(&pi, 0, sizeof(pi));
    pi.channel = SND_PCM_CHANNEL_PLAYBACK;

    if ((rtn = snd_pcm_plugin_info(data->pcmHandle, &pi)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_info failed: %s\n", snd_strerror (rtn));
        return ALC_FALSE;
    }

    // configure a sound channel
    memset(&pp, 0, sizeof (pp));
    pp.mode 		= SND_PCM_MODE_BLOCK;
    pp.channel 		= SND_PCM_CHANNEL_PLAYBACK;
    pp.start_mode 	= SND_PCM_START_FULL;
    pp.stop_mode	= SND_PCM_STOP_STOP;

    pp.buf.block.frag_size = MAX_FRAG_SIZE; //pi.max_fragment_size;
    pp.buf.block.frags_max = 2;
    pp.buf.block.frags_min = 1;

    pp.format.interleave   = 1;
    pp.format.rate 		   = DEFAULT_RATE; // pi.max_rate;
    pp.format.voices 	   = ChannelsFromDevFmt(device->FmtChans);

    switch(device->FmtType)
    {
        case DevFmtByte:	pp.format.format = SND_PCM_SFMT_S8;			break;
        case DevFmtUByte:	pp.format.format = SND_PCM_SFMT_U8;			break;
        case DevFmtShort:	pp.format.format = SND_PCM_SFMT_S16_LE;		break;
        case DevFmtUShort:	pp.format.format = SND_PCM_SFMT_U16_LE; 	break;
        case DevFmtFloat:	pp.format.format = SND_PCM_SFMT_FLOAT_LE;	break;
        default:			pp.format.format = SND_PCM_SFMT_S16_LE;		break;
    }

    // we actually don't want to block on writes
    snd_pcm_nonblock_mode(data->pcmHandle, 1);
    snd_pcm_plugin_set_disable(data->pcmHandle, PLUGIN_DISABLE_BUFFER_PARTIAL_BLOCKS);

    if ((rtn = snd_pcm_plugin_params(data->pcmHandle, &pp)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_params failed: %s\n", snd_strerror (rtn));
        return ALC_FALSE;
    }

    if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_PLAYBACK)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_prepare failed: %s\n", snd_strerror(rtn));
        return ALC_FALSE;
    }

    memset(&setup, 0, sizeof (setup));
//    setup.mode = SND_PCM_MODE_BLOCK;
    setup.channel = SND_PCM_CHANNEL_PLAYBACK;

    if ((rtn = snd_pcm_plugin_setup(data->pcmHandle, &setup)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_setup failed: %s\n", snd_strerror (rtn));
        return ALC_FALSE;
    }

    // now fill in our AL device information
	switch (setup.format.voices)
	{
		case 1:		device->FmtChans = DevFmtMono;	 break;
		case 2:		device->FmtChans = DevFmtStereo; break;
		case 4:		device->FmtChans = DevFmtQuad;	 break;
		case 6:		device->FmtChans = DevFmtX51; 	 break;
		case 7:		device->FmtChans = DevFmtX61;	 break;
		case 8:		device->FmtChans = DevFmtX71;	 break;
		default: 	device->FmtChans = DevFmtMono;	 break;
	}

	switch (setup.format.format)
	{
		case SND_PCM_SFMT_S8:
			device->FmtType = DevFmtByte;
			break;
		case SND_PCM_SFMT_U8:
			device->FmtType = DevFmtUByte;
			break;
		case SND_PCM_SFMT_S16_LE:
			device->FmtType = DevFmtShort;
			break;
		case SND_PCM_SFMT_U16_LE:
			device->FmtType = DevFmtUShort;
			break;
		case SND_PCM_SFMT_FLOAT_LE:
			device->FmtType = DevFmtFloat;
			break;
		default:
			device->FmtType = DevFmtShort;
			break;
	}

	int num_channels = ChannelsFromDevFmt(device->FmtChans);
	int frame_size = num_channels * BytesFromDevFmt(device->FmtType);

	SetDefaultChannelOrder(device);

	device->Frequency = setup.format.rate;
	device->UpdateSize = setup.buf.block.frag_size / frame_size;
	device->NumUpdates = setup.buf.block.frags + 1;

	data->size = setup.buf.block.frag_size; // device->UpdateSize * frame_size;
	data->buffer = malloc(data->size);
	if (!data->buffer)
	{
		AL_PRINT("buffer malloc failed\n");
		return ALC_FALSE;
	}

	data->thread = StartThread(QNXProc, device);

    if (data->thread == NULL)
    {
        AL_PRINT("Could not create playback thread\n");
        free(data->buffer);
        data->buffer = NULL;
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void qnx_stop_playback(ALCdevice *device)
{
    qnx_data *data = (qnx_data*)device->ExtraData;

    if (data->thread)
    {
        data->killNow = 1;
        StopThread(data->thread);
        data->thread = NULL;
    }
    data->killNow = 0;
    free(data->buffer);
    data->buffer = NULL;
}

#define NCAPTURE
//
//  Capture
//
static ALCboolean qnx_open_capture(ALCdevice *device, const ALCchar *deviceName)
{
#ifdef CAPTURE
    qnx_data 					*data;
	snd_pcm_channel_info_t 		 pi;
    snd_pcm_channel_params_t 	 pp;
    snd_pcm_channel_setup_t 	 setup;
    int 						 format;
    char 						 driver[64];
    int 						 i = -1;
    int 						 card, dev;
    int							 rtn;

    strncpy(driver, GetConfigValue("qnx", "capture", "default"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;

    data = (qnx_data*)calloc(1, sizeof(qnx_data));

    if (!deviceName || strcmp(deviceName, qnxDevice) == 0)
    {
    	if (!deviceName)
    		deviceName = qnxDevice;

        card = 0;
        dev = 0;
        i = snd_pcm_open_preferred(&data->pcmHandle, &card, &dev, SND_PCM_OPEN_CAPTURE);
    }
    else
    {
		size_t idx;

		if (!allCaptureDevNameMap)
			allCaptureDevNameMap = deviceList(SND_PCM_CHANNEL_CAPTURE, &numDevNames);

		for (idx = 0; idx < numCaptureDevNames; idx++)
		{
			if (allCaptureDevNameMap[idx].name && strcmp(deviceName, allCaptureDevNameMap[idx].name) == 0)
			{
				if (idx > 0)
					sprintf(driver, "plughw:%d,%d", allCaptureDevNameMap[idx].card, allCaptureDevNameMap[idx].dev);
				break;
			}
		}
		if (idx == numCaptureDevNames)
			return ALC_FALSE;

		i = snd_pcm_open_name(&data->pcmHandle, deviceName, SND_PCM_OPEN_CAPTURE);
    }

    if (i < 0)
    {
		free(data);
		AL_PRINT("Could not open capture device '%s': %s\n", driver, snd_strerror(i));
		return ALC_FALSE;
	}

    switch(device->FmtType)
    {
        case DevFmtByte:
            format = SND_PCM_SFMT_S8;
            break;
        case DevFmtUByte:
            format = SND_PCM_SFMT_U8;
            break;
        case DevFmtShort:
            format = SND_PCM_SFMT_S16_LE;
            break;
        case DevFmtUShort:
            format = SND_PCM_SFMT_U16_LE;
            break;
        case DevFmtFloat:
            format = SND_PCM_SFMT_FLOAT_LE;
            break;
    }

    memset(&pi, 0, sizeof(pi));
    pi.channel = SND_PCM_CHANNEL_CAPTURE;

    if ((rtn = snd_pcm_plugin_info(data->pcmHandle, &pi)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_info failed: %s\n", snd_strerror (rtn));
        return ALC_FALSE;
    }
    fprintf(stderr, "info flags: mmap %d interleave %d fragsamples %d\n", pi.flags&SND_PCM_CHNINFO_MMAP, pi.flags&SND_PCM_CHNINFO_INTERLEAVE, pi.flags&SND_PCM_CHNINFO_MMAP_VALID);
    fprintf(stderr, "info formats: %d\n", pi.formats);
    fprintf(stderr, "info rates: %d\n", pi.rates);
    fprintf(stderr, "info min rate: %d\n", pi.min_rate);
    fprintf(stderr, "info max rate: %d\n", pi.max_rate);
    fprintf(stderr, "info min_voices: %d\n", pi.min_voices);
    fprintf(stderr, "info max_buffer: %d\n", pi.max_buffer_size);
    fprintf(stderr, "info min_frag: %d\n", pi.min_fragment_size);
    fprintf(stderr, "info max_frag: %d\n", pi.max_fragment_size);


    // configure a sound channel
    memset(&pp, 0, sizeof (pp));
    pp.mode 		= SND_PCM_MODE_BLOCK;
    pp.channel 		= SND_PCM_CHANNEL_CAPTURE;
    pp.start_mode 	= SND_PCM_START_DATA;
    pp.stop_mode	= SND_PCM_STOP_STOP;

    pp.buf.block.frag_size = device->UpdateSize; //pi.max_fragment_size;
    pp.buf.block.frags_max = device->NumUpdates + 1;  // 2;
    pp.buf.block.frags_min = 1;

    pp.format.interleave   = 1;
    pp.format.rate 		   = device->Frequency; // pi.max_rate;  // 48000
    pp.format.voices 	   = ChannelsFromDevFmt(device->FmtChans);

    switch(device->FmtType)
    {
        case DevFmtByte:	pp.format.format = SND_PCM_SFMT_S8;			break;
        case DevFmtUByte:	pp.format.format = SND_PCM_SFMT_U8;			break;
        case DevFmtShort:	pp.format.format = SND_PCM_SFMT_S16_LE;		break;
        case DevFmtUShort:	pp.format.format = SND_PCM_SFMT_U16_LE; 	break;
        case DevFmtFloat:	pp.format.format = SND_PCM_SFMT_FLOAT_LE;	break;
        default:			pp.format.format = SND_PCM_SFMT_S16_LE;		break;
    }

    // we actually don't want to block on reads
//    snd_pcm_nonblock_mode(data->pcmHandle, 1);

    if ((rtn = snd_pcm_plugin_params(data->pcmHandle, &pp)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_params failed: %s\n", snd_strerror (rtn));
        return ALC_FALSE;
    }

    if ((rtn = snd_pcm_channel_prepare(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_prepare failed: %s\n", snd_strerror(rtn));
        return ALC_FALSE;
    }

    memset(&setup, 0, sizeof (setup));
//    setup.mode = SND_PCM_MODE_BLOCK;
    setup.channel = SND_PCM_CHANNEL_CAPTURE;

    if ((rtn = snd_pcm_plugin_setup(data->pcmHandle, &setup)) < 0)
    {
    	AL_PRINT("snd_pcm_plugin_setup failed: %s\n", snd_strerror (rtn));
        return ALC_FALSE;
    }


	int num_channels = ChannelsFromDevFmt(device->FmtChans);
	int frame_size = num_channels * BytesFromDevFmt(device->FmtType);

	device->Frequency = setup.format.rate;
	device->UpdateSize = setup.buf.block.frag_size / frame_size;
	device->NumUpdates = setup.buf.block.frags + 1;

	data->size = setup.buf.block.frag_size; // device->UpdateSize * frame_size;
	data->buffer = malloc(data->size);
    if (!data->buffer)
    {
        AL_PRINT("buffer malloc failed\n");
        goto error;
    }

    data->ring = CreateRingBuffer(frame_size, device->UpdateSize * device->NumUpdates);
    if (!data->ring)
    {
        AL_PRINT("ring buffer create failed\n");
        goto error;
    }

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = data;
    return ALC_TRUE;

error:
    free(data->buffer);
    DestroyRingBuffer(data->ring);
    snd_pcm_close(data->pcmHandle);
    free(data);

    device->ExtraData = NULL;
#endif
    return ALC_FALSE;
}

static void qnx_close_capture(ALCdevice *device)
{
#ifdef CAPTURE
    qnx_data *data = (qnx_data*)device->ExtraData;

    snd_pcm_close(data->pcmHandle);
    DestroyRingBuffer(data->ring);

    free(data->buffer);
    free(data);
    device->ExtraData = NULL;
#endif
}

static void qnx_start_capture(ALCdevice *device)
{
#ifdef CAPTURE
    qnx_data *data = (qnx_data*)device->ExtraData;

#if 0
    int err;

    err = snd_pcm_start(data->pcmHandle);
    if (err < 0)
    {
        AL_PRINT("start failed: %s\n", psnd_strerror(err));
        aluHandleDisconnect(device);
    }
    else
#endif
        data->doCapture = AL_TRUE;
#endif
}

static void qnx_stop_capture(ALCdevice *device)
{
#ifdef CAPTURE
    qnx_data *data = (qnx_data*)device->ExtraData;
    snd_pcm_plugin_flush(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE);
    data->doCapture = AL_FALSE;
#endif
}

static ALCuint qnx_available_samples(ALCdevice *device)
{
#ifdef CAPTURE
    qnx_data *data = (qnx_data*)device->ExtraData;

    /*
    snd_pcm_sframes_t avail;

    avail = (device->Connected ? snd_pcm_avail_update(data->pcmHandle) : 0);
    if (avail < 0)
    {
        AL_PRINT("avail update failed: %s\n", psnd_strerror(avail));

        if ((avail=snd_pcm_recover(data->pcmHandle, avail, 1)) >= 0)
        {
            if (data->doCapture)
                avail = snd_pcm_start(data->pcmHandle);
            if (avail >= 0)
                avail = snd_pcm_avail_update(data->pcmHandle);
        }
        if (avail < 0)
        {
            AL_PRINT("restore error: %s\n", psnd_strerror(avail));
            aluHandleDisconnect(device);
        }
    }

    while (avail > 0)
    {
        snd_pcm_sframes_t amt;

        amt = psnd_pcm_bytes_to_frames(data->pcmHandle, data->size);
        if (avail < amt)
        	amt = avail;
*/
    	int amt;

        amt = snd_pcm_plugin_read(data->pcmHandle, data->buffer, data->size);

fprintf(stderr, "read %d bytes\n", amt);
        if (amt < 0)
        {
			if (amt == -EAGAIN )
				return 0;

			snd_pcm_channel_status_t status;

			memset(&status, 0, sizeof (status));
			status.channel = SND_PCM_CHANNEL_CAPTURE;

			int rtn = snd_pcm_plugin_status(data->pcmHandle, &status);

			// we need to reinitialize the sound channel if we've underrun the buffer
			if (status.status == SND_PCM_STATUS_OVERRUN)
			{
				fprintf (stderr, "overrun at position %u!!!\n", status.scount);
				if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_CAPTURE)) < 0)
				{
					AL_PRINT("Invalid state detected for playback: %s\n", snd_strerror(rtn));
					aluHandleDisconnect(device);
				}
				return 0;
			}
			else if (status.status == SND_PCM_STATUS_RUNNING)
			{
				return 0; 										/* everything is ok, but the driver is waiting for data */
			}
			else
			{
                AL_PRINT("restore error: %s\n", psnd_strerror(amt));
                aluHandleDisconnect(device);
                return 0;
			}
        }

        WriteRingBuffer(data->ring, data->buffer, amt);
//        avail -= amt;


    return RingBufferSize(data->ring);
#else
    return 0;
#endif
}

static void qnx_capture_samples(ALCdevice *device, ALCvoid *buffer, ALCuint samples)
{
#ifdef CAPTURE
    qnx_data *data = (qnx_data*)device->ExtraData;

    if (samples <= qnx_available_samples(device))
        ReadRingBuffer(data->ring, buffer, samples);
    else
        alcSetError(device, ALC_INVALID_VALUE);
#endif
}


BackendFuncs qnx_funcs = {
    qnx_open_playback,
    qnx_close_playback,
    qnx_reset_playback,
    qnx_stop_playback,
    qnx_open_capture,
    qnx_close_capture,
    qnx_start_capture,
    qnx_stop_capture,
    qnx_capture_samples,
    qnx_available_samples
};

void alc_qnx_init(BackendFuncs *func_list)
{
    *func_list = qnx_funcs;
}

void alc_qnx_deinit(void)
{
    ALuint i;

    for (i = 0;i < numDevNames;++i)
        free(allDevNameMap[i].name);

    free(allDevNameMap);
    allDevNameMap = NULL;
    numDevNames = 0;

    for (i = 0;i < numCaptureDevNames;++i)
        free(allCaptureDevNameMap[i].name);

    free(allCaptureDevNameMap);
    allCaptureDevNameMap = NULL;
    numCaptureDevNames = 0;
}

void alc_qnx_probe(int type)
{
    ALuint i;

    if (type == DEVICE_PROBE)
    {
        AppendDeviceList(qnxDevice);
    }
    else if (type == ALL_DEVICE_PROBE)
    {
        for (i = 0; i < numDevNames; ++i)
            free(allDevNameMap[i].name);

        free(allDevNameMap);
        allDevNameMap = deviceList(SND_PCM_CHANNEL_PLAYBACK, &numDevNames);

        for (i = 0; i < numDevNames; ++i)
            AppendAllDeviceList(allDevNameMap[i].name);
    }
#ifdef CAPTURE
    else if (type == CAPTURE_DEVICE_PROBE)
    {
        for (i = 0; i < numCaptureDevNames; ++i)
            free(allCaptureDevNameMap[i].name);

        free(allCaptureDevNameMap);
        allCaptureDevNameMap = deviceList(SND_PCM_CHANNEL_CAPTURE, &numCaptureDevNames);

        for (i = 0; i < numCaptureDevNames; ++i)
            AppendCaptureDeviceList(allCaptureDevNameMap[i].name);
    }
#endif
}
