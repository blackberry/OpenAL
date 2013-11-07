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
    snd_pcm_t 		 		 *pcmHandle;
    snd_pcm_t				 *pcmHandleCapture;
    snd_pcm_channel_status_t  status;

    ALvoid 					 *buffer;
    ALsizei 				  size;

    ALboolean 				  doCapture;
    RingBuffer 				 *ring;

    pthread_mutex_t 		  mutex;
    volatile int 			  killWriter;
    ALvoid 					 *writerThread;
} asound_data;

typedef struct {
    ALCchar *name;
    char *device;
    int card, dev;
} DevMap;

static const ALCchar asoundDevice[] = "ASOUND Default";
static const ALCchar qnxDevice[] = "QNX Default";
static DevMap *allDevNameMap;
static DevMap *allCaptureDevNameMap;
static ALuint  numDevNames;
static ALuint  numCaptureDevNames;

#define MAX_FRAG_SIZE 3072
#define DEFAULT_RATE 48000

static const char *prefix_name(int stream)
{
    assert(stream == SND_PCM_CHANNEL_PLAYBACK || stream == SND_PCM_CHANNEL_CAPTURE);
    return (stream == SND_PCM_CHANNEL_PLAYBACK) ? "device-prefix" : "capture-prefix";
}

static DevMap *probe_devices(int stream, ALuint *count)
{
    const char 				*main_prefix = "plughw:";
    snd_ctl_t 				*handle;
	snd_pcm_info_t 			 pcminfo;
#ifdef VERBOSE
	snd_pcm_channel_info_t   chninfo;
#endif
	struct snd_ctl_hw_info 	 info;
	int     				 max_cards, card, rtn, dev, num_devices;
    DevMap 					*dev_list;

    dev_list = malloc(sizeof(DevMap) * 1);
    dev_list[0].name = strdup(asoundDevice);
    dev_list[0].device = strdup(GetConfigValue("asound", (stream == SND_PCM_CHANNEL_PLAYBACK) ? "device" : "capture", "default"));

	num_devices = 1;
	max_cards = snd_cards();

    if (max_cards <= 0)
    {
        ERR("Failed to find a card: %s\n", snd_strerror(max_cards));
        return NULL;
    }

    ConfigValueStr("asound", prefix_name(stream), &main_prefix);

	for (card = 0; card < max_cards; card++)
    //while (card >= 0)
    {
        const char *card_prefix = main_prefix;
        const char *cardname;
        int cardid;
        char name[256];

        snprintf(name, sizeof(name), "hw:%d", card);

        if ((rtn = snd_ctl_open(&handle, card)) < 0)
        {
            ERR("control open (hw:%d): %s\n", card, snd_strerror(rtn));
            continue;
        }
        if ((rtn = snd_ctl_hw_info(handle, &info)) < 0)
        {
            ERR("control hardware info (hw:%d): %s\n", card, snd_strerror(rtn));
            snd_ctl_close(handle);
            continue;
        }

        cardname = info.name;
        cardid = card;

        snprintf(name, sizeof(name), "%s-%d", prefix_name(stream), cardid);
        ConfigValueStr("asound", name, &card_prefix);

		for (dev = 0; dev < info.pcmdevs; dev++)
		{
			const char *devname;
			void *temp;

			if ((rtn = snd_ctl_pcm_info(handle, dev, &pcminfo)) < 0)
			{
				if (rtn != -ENOENT)
				    WARN("control digital audio info (hw:%d): %s\n", card, snd_strerror(rtn));
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

			int idx;
			if (pcminfo.flags & SND_PCM_INFO_PLAYBACK)
			{
				for (idx = 0; idx <= pcminfo.playback; idx++)
				{
					memset (&chninfo, 0, sizeof (chninfo));
					chninfo.channel = SND_PCM_CHANNEL_PLAYBACK;
					if ((rtn = snd_ctl_pcm_channel_info(handle, dev, SND_PCM_CHANNEL_PLAYBACK, idx, &chninfo)) < 0)
					{
						fprintf(stderr, "Error: control digital audio playback info (%i): %s\n", card, snd_strerror(rtn));
					}
					else
					{
						fprintf(stderr, "  Playback subdevice #%i: %s\n", idx, chninfo.subname);
					}
				}
			}
			if (pcminfo.flags & SND_PCM_INFO_CAPTURE)
			{
				for (idx = 0; idx <= pcminfo.capture; idx++)
				{
					memset (&chninfo, 0, sizeof (chninfo));
					chninfo.channel = SND_PCM_CHANNEL_CAPTURE;
					if ((rtn = snd_ctl_pcm_channel_info (handle, dev, SND_PCM_CHANNEL_CAPTURE, 0, &chninfo)) < 0)
					{
						fprintf(stderr, "Error: control digital audio capture info (%i): %s\n", card, snd_strerror(rtn));
					}
					else
					{
						fprintf(stderr, "  Capture subdevice #%i: %s\n", idx, chninfo.subname);
					}
				}
			}
#endif

            temp = realloc(dev_list, sizeof(DevMap) * (num_devices+1));
            if (temp)
            {
                const char *device_prefix = card_prefix;
                char device[128];

                dev_list = temp;
                devname = pcminfo.name;

                snprintf(name, sizeof(name), "%s-%d-%d", prefix_name(stream), cardid, dev+1);
                ConfigValueStr("asound", name, &device_prefix);

                snprintf(name, sizeof(name), "%s, %s (CARD=%d,DEV=%d)", cardname, devname, cardid, dev+1);
                snprintf(device, sizeof(device), "%sCARD=%d,DEV=%d", device_prefix, cardid, dev+1);

                TRACE("Got device \"%s\", \"%s\"\n", name, device);
                dev_list[num_devices].name = strdup(name);
                dev_list[num_devices].device = strdup(device);
                dev_list[num_devices].card = card;
                dev_list[num_devices].dev  = dev+1;
                num_devices++;
            }
        }
        snd_ctl_close(handle);
    }

    *count = num_devices;
    return dev_list;
}

static ALCboolean al_mutex_create(pthread_mutex_t *mutex)
{
	pthread_mutexattr_t attr;
	ALCboolean rc = pthread_mutexattr_init(&attr);

	if (rc == 0)
	{
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
		rc = pthread_mutex_init(mutex, &attr);

		pthread_mutexattr_destroy(&attr);

		if (rc != 0)
		{
			ERR("mutex_create: Failure in pthread_mutex_init [%d]", rc);
			return ALC_FALSE;
		}
	}
	else
	{
		ERR("mutex_create: Failure in pthread_mutexattr_init [%d]", rc);
		return ALC_FALSE;
	}

    return ALC_TRUE;
}

//
//  writer thread + pcm paramaterization
//
static int check_pcm_status(asound_data *data, int channel_type)
{
	int rtn = EOK;

	memset(&data->status, 0, sizeof (data->status));
	data->status.channel = channel_type;

	if ((rtn = snd_pcm_plugin_status(data->pcmHandle, &data->status)) == 0)
	{
		if (data->status.status == SND_PCM_STATUS_UNSECURE)
		{
			ERR("check_pcm_status got SND_PCM_STATUS_UNSECURE, aborting playback\n");
			rtn = -EPROTO;
		}
		else if (data->status.status == SND_PCM_STATUS_UNDERRUN)
		{
			if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, channel_type)) < 0)
			{
				ERR("Invalid state detected for underrun on snd_pcm_plugin_prepare: %s\n", snd_strerror(rtn));
				rtn = -EPROTO;
			}
		}
		else if (data->status.status == SND_PCM_STATUS_OVERRUN)
		{
			if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, channel_type)) < 0)
			{
				ERR("Invalid state detected for overrun on snd_pcm_plugin_prepare: %s\n", snd_strerror(rtn));
				rtn = -EPROTO;
			}
		}
		else if (data->status.status == SND_PCM_STATUS_CHANGE)
		{
			if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, channel_type)) < 0)
			{
				ERR("Invalid state detected for change on snd_pcm_plugin_prepare: %s\n", snd_strerror(rtn));
				rtn = -EPROTO;
			}
		}
	}
	else
	{
		ERR("check_pcm_status failed: %s\n", snd_strerror(rtn));
		if (rtn == -ESRCH)
			rtn = -EBADF;
	}

	return rtn;
}

static ALCboolean reset_pcm_channel(ALCdevice *device)
{
    asound_data 			   *data = (asound_data*)device->ExtraData;
	snd_pcm_channel_info_t 		pi;
    snd_pcm_channel_params_t 	pp;
    snd_pcm_channel_setup_t 	setup;
    int 						rtn;

    memset(&pi, 0, sizeof(pi));
    pi.channel = SND_PCM_CHANNEL_PLAYBACK;

    if ((rtn = snd_pcm_plugin_info(data->pcmHandle, &pi)) < 0)
    {
    	ERR("snd_pcm_plugin_info failed: %s\n", snd_strerror(rtn));
        return ALC_FALSE;
    }

    // configure a sound channel
    memset(&pp, 0, sizeof(pp));
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

    switch (device->FmtType)
    {
        case DevFmtByte:	pp.format.format = SND_PCM_SFMT_S8;			break;
        case DevFmtUByte:	pp.format.format = SND_PCM_SFMT_U8;			break;
        case DevFmtShort:	pp.format.format = SND_PCM_SFMT_S16_LE;		break;
        case DevFmtUShort:	pp.format.format = SND_PCM_SFMT_U16_LE; 	break;
        case DevFmtFloat:	pp.format.format = SND_PCM_SFMT_FLOAT_LE;	break;
        case DevFmtInt:		pp.format.format = SND_PCM_SFMT_S32_LE;		break;
        case DevFmtUInt:	pp.format.format = SND_PCM_SFMT_U32_LE;		break;
        default:			pp.format.format = SND_PCM_SFMT_S16_LE;		break;
    }

    snd_pcm_plugin_set_disable(data->pcmHandle, PLUGIN_DISABLE_BUFFER_PARTIAL_BLOCKS);

    if ((rtn = snd_pcm_plugin_params(data->pcmHandle, &pp)) < 0)
    {
    	ERR("snd_pcm_plugin_params failed: %s\n", snd_strerror(rtn));
        return ALC_FALSE;
    }

    if ((rtn = snd_pcm_plugin_prepare(data->pcmHandle, SND_PCM_CHANNEL_PLAYBACK)) < 0)
    {
    	ERR("snd_pcm_plugin_prepare failed: %s\n", snd_strerror(rtn));
        return ALC_FALSE;
    }

    memset(&setup, 0, sizeof(setup));
    setup.channel = SND_PCM_CHANNEL_PLAYBACK;

    if ((rtn = snd_pcm_plugin_setup(data->pcmHandle, &setup)) < 0)
    {
    	ERR("snd_pcm_plugin_setup failed: %s\n", snd_strerror(rtn));
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
		default: 	device->FmtChans = DevFmtStereo; break;
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
		ERR("buffer malloc failed\n");
		return ALC_FALSE;
	}

	return ALC_TRUE;
}

static ALCboolean open_pcm_device(ALCdevice *device, const ALCchar *deviceName)
{
	asound_data *data = (asound_data*)device->ExtraData;
	char driver[128];
	int  card, dev;
	int  rtn = -1;

	strncpy(driver, GetConfigValue("asound", "device", "default"), sizeof(driver)-1);
	driver[sizeof(driver)-1] = 0;

	// see if it's the default device (check backwards compatibility with qnxDevice as well)
	if (!deviceName || strcmp(deviceName, asoundDevice) == 0 || strcmp(deviceName, qnxDevice) == 0)
	{
		if (!deviceName)
			deviceName = asoundDevice;

		card = 0;
		dev = 0;
		rtn = snd_pcm_open_preferred(&data->pcmHandle, &card, &dev, SND_PCM_OPEN_PLAYBACK);
		if (rtn < 0)
		{
			ERR("Could not open preferred playback device: %s\n", snd_strerror(rtn));
			return ALC_FALSE;
		}
	}
	// see if it's a device path
	else if (strncmp(deviceName, "/dev", 4) == 0)
	{
		rtn = snd_pcm_open_name(&data->pcmHandle, (char *)deviceName, SND_PCM_OPEN_PLAYBACK);
		if (rtn < 0)
		{
			return ALC_FALSE;
		}
	}
	// it must be in our device list then
	else
	{
		size_t idx;

		if (!allDevNameMap)
			allDevNameMap = probe_devices(SND_PCM_CHANNEL_PLAYBACK, &numDevNames);

		for (idx = 0; idx < numDevNames; idx++)
		{
			if (allDevNameMap[idx].name && strcmp(deviceName, allDevNameMap[idx].name) == 0)
			{
				if (idx > 0)
					strcpy(driver, allDevNameMap[idx].name);

				break;
			}
		}
		if (idx == numDevNames)
		{
			ERR("Could not find the device %s\n", deviceName);
			return ALC_FALSE;
		}

		rtn = snd_pcm_open(&data->pcmHandle, allDevNameMap[idx].card, allDevNameMap[idx].dev, SND_PCM_OPEN_PLAYBACK);

		if (rtn < 0)
		{
			ERR("Could not open playback device '%s': %s\n", driver, snd_strerror(rtn));
			return ALC_FALSE;
		}
	}

	snd_pcm_plugin_set_disable(data->pcmHandle, PLUGIN_MMAP);
	if ((rtn = snd_pcm_plugin_set_enable(data->pcmHandle, PLUGIN_ROUTING)) < 0)
	{
		ERR("snd_pcm_plugin_set_enable failed: %s\n", snd_strerror(rtn));
	    return -1;
	}

	if (device->szDeviceName)
		free(device->szDeviceName);

	device->szDeviceName = strdup(deviceName);

	return ALC_TRUE;
}

static ALuint AsoundProc(ALvoid *ptr)
{
	ALCdevice *device = (ALCdevice*)ptr;
	asound_data  *data   = (asound_data*)device->ExtraData;
	char 	  *write_ptr;
	int 	   avail;
	ALint      len;
	int		   rtn;

	SetRTPriority();

	ALint frame_size = FrameSizeFromDevFmt(device->FmtChans, device->FmtType);

	while (!data->killWriter)
	{
		len = data->size;
		write_ptr = data->buffer;

		avail = len / frame_size;
		aluMixData(device, write_ptr, avail);

		while (len > 0 && !data->killWriter)
		{
			int wrote = snd_pcm_plugin_write(data->pcmHandle, write_ptr, len);

			if (wrote <= 0)
			{
				if (wrote == -EAGAIN)
				{
					continue;
				}

				rtn = check_pcm_status(data, SND_PCM_CHANNEL_PLAYBACK);

				if (rtn == -EBADF)
				{
					// hdmi may be disconnected here, break out of the loop while we're in flux
					break;
				}
				else if (rtn == -EPROTO)
				{
					// bad error, let's disconnect the audio
					aluHandleDisconnect(device);
					break;
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

static ALCenum asound_open_playback(ALCdevice *device, const ALCchar *deviceName)
{

	asound_data *data = (asound_data*)calloc(1, sizeof(asound_data));
	device->ExtraData = data;

	if (open_pcm_device(device, deviceName) == ALC_FALSE)
	{
		free(data);
		device->ExtraData = 0;
		return ALC_INVALID_VALUE;
	}

	if (al_mutex_create(&data->mutex) == ALC_FALSE)
		return ALC_OUT_OF_MEMORY;
	return ALC_NO_ERROR;
}

static void asound_close_playback(ALCdevice *device)
{
	asound_data *data = (asound_data*)device->ExtraData;

	snd_pcm_close(data->pcmHandle);
	pthread_mutex_destroy(&data->mutex);

	device->ExtraData = NULL;
	free(data);
}

static ALCboolean asound_reset_playback(ALCdevice *device)
{
	return reset_pcm_channel(device);
}

static ALCboolean asound_start_playback(ALCdevice *device)
{
    asound_data *data = (asound_data*)device->ExtraData;

	data->writerThread = StartThread(AsoundProc, device);

    if (data->writerThread == NULL)
    {
        ERR("Could not create writer thread\n");
        free(data->buffer);
        data->buffer = NULL;
        return ALC_FALSE;
    }

    return ALC_TRUE;
}

static void asound_stop_playback(ALCdevice *device)
{
    asound_data *data = (asound_data*)device->ExtraData;

    if (data->writerThread)
    {
        data->killWriter = 1;
        StopThread(data->writerThread);
        data->writerThread = NULL;
    }
    data->killWriter = 0;
    free(data->buffer);
    data->buffer = NULL;
}

//
//  Capture
//
static ALCenum asound_open_capture(ALCdevice *device, const ALCchar *deviceName)
{
    asound_data 				*data;
	snd_pcm_channel_info_t 		 pi;
    snd_pcm_channel_params_t 	 pp;
    snd_pcm_channel_setup_t 	 setup;
    char 						 driver[64];
    int 						 i = -1;
    int 						 card, dev;
    int							 rtn;

    strncpy(driver, GetConfigValue("asound", "capture", "default"), sizeof(driver)-1);
    driver[sizeof(driver)-1] = 0;

    data = (asound_data*)calloc(1, sizeof(asound_data));

    if (!deviceName || strcmp(deviceName, asoundDevice) == 0)
    {
    	if (!deviceName)
    		deviceName = asoundDevice;

        card = 0;
        dev = 0;

        i = snd_pcm_open_preferred(&data->pcmHandleCapture, &card, &dev, SND_PCM_OPEN_CAPTURE);
    }
    else
    {
		size_t idx;

		if (!allCaptureDevNameMap)
			allCaptureDevNameMap = probe_devices(SND_PCM_CHANNEL_CAPTURE, &numCaptureDevNames);

		for (idx = 0; idx < numCaptureDevNames; idx++)
		{
			if (allCaptureDevNameMap[idx].name && strcmp(deviceName, allCaptureDevNameMap[idx].name) == 0)
			{
				if (idx > 0)
					sprintf(driver, "hw:%d,%d", allCaptureDevNameMap[idx].card, allCaptureDevNameMap[idx].dev);
				break;
			}
		}
		if (idx == numCaptureDevNames)
			return ALC_INVALID_VALUE;

		i = snd_pcm_open(&data->pcmHandleCapture, allCaptureDevNameMap[idx].card, allCaptureDevNameMap[idx].dev, SND_PCM_OPEN_CAPTURE);
    }

    if (i < 0)
    {
		free(data);
		ERR("Could not open capture device '%s': %s\n", driver, snd_strerror(i));
		return ALC_INVALID_VALUE;
	}

	snd_pcm_plugin_set_disable(data->pcmHandleCapture, PLUGIN_MMAP);

	if ((rtn = snd_pcm_plugin_set_enable(data->pcmHandleCapture, PLUGIN_ROUTING)) < 0)
	{
		ERR("snd_pcm_plugin_set_enable failed: %s\n", snd_strerror(rtn));
	    return -1;
	}

    memset(&pi, 0, sizeof(pi));
    pi.channel = SND_PCM_CHANNEL_CAPTURE;

    if ((rtn = snd_pcm_plugin_info(data->pcmHandleCapture, &pi)) < 0)
    {
    	ERR("snd_pcm_plugin_info failed: %s\n", snd_strerror (rtn));
        return ALC_INVALID_VALUE;
    }

    // configure a sound channel
    memset(&pp, 0, sizeof (pp));
    pp.mode 		= SND_PCM_MODE_BLOCK;
    pp.channel 		= SND_PCM_CHANNEL_CAPTURE;
    pp.start_mode 	= SND_PCM_START_DATA;
    pp.stop_mode	= SND_PCM_STOP_STOP;

    pp.buf.block.frag_size = device->UpdateSize;
    pp.buf.block.frags_max = -1;
    pp.buf.block.frags_min = 1;

    pp.format.interleave   = 1;
    pp.format.rate 		   = device->Frequency;
    pp.format.voices 	   = ChannelsFromDevFmt(device->FmtChans);

    switch(device->FmtType)
    {
        case DevFmtByte:	pp.format.format = SND_PCM_SFMT_S8;			break;
        case DevFmtUByte:	pp.format.format = SND_PCM_SFMT_U8;			break;
        case DevFmtShort:	pp.format.format = SND_PCM_SFMT_S16_LE;		break;
        case DevFmtUShort:	pp.format.format = SND_PCM_SFMT_U16_LE; 	break;
        case DevFmtFloat:	pp.format.format = SND_PCM_SFMT_FLOAT_LE;	break;
        case DevFmtInt:		pp.format.format = SND_PCM_SFMT_S32_LE;		break;
        case DevFmtUInt:	pp.format.format = SND_PCM_SFMT_U32_LE;		break;
        default:			pp.format.format = SND_PCM_SFMT_S16_LE;		break;
    }

    if ((rtn = snd_pcm_plugin_params(data->pcmHandleCapture, &pp)) < 0)
    {
    	ERR("snd_pcm_plugin_params failed: %s\n", snd_strerror (rtn));
        return ALC_INVALID_VALUE;
    }

    if ((rtn = snd_pcm_channel_prepare(data->pcmHandleCapture, SND_PCM_CHANNEL_CAPTURE)) < 0)
    {
    	ERR("snd_pcm_plugin_prepare failed: %s\n", snd_strerror(rtn));
        return ALC_INVALID_VALUE;
    }

    memset(&setup, 0, sizeof (setup));
    setup.channel = SND_PCM_CHANNEL_CAPTURE;

    if ((rtn = snd_pcm_plugin_setup(data->pcmHandleCapture, &setup)) < 0)
    {
    	ERR("snd_pcm_plugin_setup failed: %s\n", snd_strerror (rtn));
        return ALC_INVALID_VALUE;
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

	int frame_size = FrameSizeFromDevFmt(device->FmtChans, device->FmtType);

	device->Frequency = setup.format.rate;
//	device->UpdateSize = setup.buf.block.frag_size / frame_size;
	device->NumUpdates = setup.buf.block.frags + 1;

	data->size = setup.buf.block.frag_size;
	data->buffer = malloc(data->size);
    if (!data->buffer)
    {
        ERR("buffer malloc failed\n");
        goto error;
    }

    data->ring = CreateRingBuffer(frame_size, device->UpdateSize);
    if (!data->ring)
    {
        ERR("ring buffer create failed\n");
        goto error;
    }

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = data;
    return ALC_NO_ERROR;

error:
    free(data->buffer);
    DestroyRingBuffer(data->ring);
    snd_pcm_close(data->pcmHandleCapture);
    free(data);

    device->ExtraData = NULL;

    return ALC_OUT_OF_MEMORY;
}

static void asound_close_capture(ALCdevice *device)
{
    asound_data *data = (asound_data*)device->ExtraData;

    snd_pcm_close(data->pcmHandleCapture);
    DestroyRingBuffer(data->ring);

    free(data->buffer);
    free(data);
    device->ExtraData = NULL;
}

static void asound_start_capture(ALCdevice *device)
{
    asound_data *data = (asound_data*)device->ExtraData;
    data->doCapture = AL_TRUE;
}

static void asound_stop_capture(ALCdevice *device)
{
    asound_data *data = (asound_data*)device->ExtraData;
    snd_pcm_plugin_flush(data->pcmHandleCapture, SND_PCM_CHANNEL_CAPTURE);
    data->doCapture = AL_FALSE;
}

static ALCuint asound_available_samples(ALCdevice *device)
{
    asound_data *data = (asound_data*)device->ExtraData;

    int amt;
    ALint frame_size;

    if (data->doCapture)
    {
		frame_size = FrameSizeFromDevFmt(device->FmtChans, device->FmtType);

		amt = snd_pcm_plugin_read(data->pcmHandleCapture, data->buffer, data->size);

		if (amt < 0)
		{
			if (amt == -EAGAIN)
				return 0;

			snd_pcm_channel_status_t status;

			memset(&status, 0, sizeof (status));
			status.channel = SND_PCM_CHANNEL_CAPTURE;

			int rtn = snd_pcm_plugin_status(data->pcmHandleCapture, &status);

			// we need to reinitialize the sound channel if we've overrun the buffer
			if (status.status == SND_PCM_STATUS_READY ||
				status.status == SND_PCM_STATUS_OVERRUN ||
				status.status == SND_PCM_STATUS_CHANGE)
			{
				if ((rtn = snd_pcm_plugin_prepare(data->pcmHandleCapture, SND_PCM_CHANNEL_CAPTURE)) < 0)
				{
					ERR("Invalid state detected for capture: %s\n", snd_strerror(rtn));
					aluHandleDisconnect(device);
				}
				return 0;
			}
		}

		WriteRingBuffer(data->ring, data->buffer, amt/frame_size);
    }

    return RingBufferSize(data->ring);
}

static ALCenum asound_capture_samples(ALCdevice *device, ALCvoid *buffer, ALCuint samples)
{
    asound_data *data = (asound_data*)device->ExtraData;

    if (data->ring)
        ReadRingBuffer(data->ring, buffer, samples);
    else
        return ALC_INVALID_VALUE;

    return ALC_NO_ERROR;
}

BackendFuncs asound_funcs = {
    asound_open_playback,
    asound_close_playback,
    asound_reset_playback,
    asound_start_playback,
    asound_stop_playback,
    asound_open_capture,
    asound_close_capture,
    asound_start_capture,
    asound_stop_capture,
    asound_capture_samples,
    asound_available_samples
};

ALCboolean alc_asound_init(BackendFuncs *func_list)
{
    *func_list = asound_funcs;

    return ALC_TRUE;
}

void alc_asound_deinit(void)
{
    ALuint i;

    for(i = 0; i < numDevNames; ++i)
    {
        free(allDevNameMap[i].name);
        free(allDevNameMap[i].device);
    }
    free(allDevNameMap);
    allDevNameMap = NULL;
    numDevNames = 0;

    for(i = 0; i < numCaptureDevNames; ++i)
    {
        free(allCaptureDevNameMap[i].name);
        free(allCaptureDevNameMap[i].device);
    }
    free(allCaptureDevNameMap);
    allCaptureDevNameMap = NULL;
    numCaptureDevNames = 0;
}

void alc_asound_probe(enum DevProbe type)
{
    ALuint i;

    switch(type)
    {
        case ALL_DEVICE_PROBE:
            for (i = 0; i < numDevNames; ++i)
            {
                free(allDevNameMap[i].name);
                free(allDevNameMap[i].device);
            }

            free(allDevNameMap);
            allDevNameMap = probe_devices(SND_PCM_CHANNEL_PLAYBACK, &numDevNames);

            for (i = 0; i < numDevNames; ++i)
                AppendAllDeviceList(allDevNameMap[i].name);
            break;

        case CAPTURE_DEVICE_PROBE:
            for (i = 0; i < numCaptureDevNames; ++i)
            {
                free(allCaptureDevNameMap[i].name);
                free(allCaptureDevNameMap[i].device);
            }

            free(allCaptureDevNameMap);
            allCaptureDevNameMap = probe_devices(SND_PCM_CHANNEL_CAPTURE, &numCaptureDevNames);

            for (i = 0; i < numCaptureDevNames; ++i)
                AppendCaptureDeviceList(allCaptureDevNameMap[i].name);
            break;
    }
}

