#include "qua_version.h"

#if defined(QUA_V_AUDIO) && defined(QUA_V_AUDIO_ASIO)

#if defined(WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "QuaAsio.h"
#include "QuaAudio.h"
#include "Channel.h"
#include "FloatQueue.h"
#include "Qua.h"
#include "Clock.h"

#include "QuaEnvironment.h"

#include <iostream>

long			QuaAsio::bufSize;
long			QuaAsio::minBufSize;
long			QuaAsio::maxBufSize;
long			QuaAsio::preferredBufSize;
long			QuaAsio::bufSizeGranularity;
long			QuaAsio::nActiveInput;
long			QuaAsio::nActiveOutput;
ASIOBufferInfo	*QuaAsio::buffers;
vector<QuaAudioIn*>QuaAsio::input;
vector<QuaAudioOut*>QuaAsio::output;
//Qua				*QuaAsio::uberQua;
QuaAudioManager	&QuaAsio::audio = getAudioManager();
bool			QuaAsio::outputReady;
ASIOCallbacks	QuaAsio::callbacks;

flag debug_asio = 0;

QuaAsio::QuaAsio()
{
	nDrivers = drivers.asioGetNumDev();
	if (nDrivers > 0) {
		char	**names = new char*[nDrivers];
		int		i;
		for (i=0; i<nDrivers; i++) {
			names[i] = new char[MAXDRVNAMELEN];
		}
		nDrivers = drivers.getDriverNames(names, nDrivers);
		installedDrivers = names;
	} else {
		installedDrivers = nullptr;
	}
	loaded = false;
	outputReady = false;

	nInputChannels = 0;
	nOutputChannels = 0;

	buffers = nullptr;
	nActiveInput = 0;
	nActiveOutput = 0;
}

QuaAsio::~QuaAsio()
{

	if (installedDrivers) {
		for (short i=0; i<nDrivers; i++) {
			delete installedDrivers[i];
		}
		delete installedDrivers;
	}
}

void
QuaAsio::SetPreferredDriver(char *p)
{
	if (p && *p) {
		preferredDriver = p;
	} else {
		preferredDriver = "";
	}
}

char *
QuaAsio::DriverName(int32 n)
{
	if (n>= 0 && n<nDrivers) {
		char	*p = installedDrivers[n];
		if (p && *p) {
			return p;
		}
	}
	return nullptr;			
}

status_t
QuaAsio::Load()
{
	status_t	err;
	if (loaded) {
		return B_OK;
	}
	if (CurrentDriver() == nullptr) {
		err = (! ASE_OK);
		if (preferredDriver.size()) {
			err = LoadDriver(const_cast<char*>(preferredDriver.c_str()));
		}

		if (err != ASE_OK) {
			for (short i=0; i<nDrivers; i++) {
				err = LoadDriver(DriverName(i));
				if (err == ASE_OK) {
					break;
				}
			}
		}

		if (err != ASE_OK) {
			return err;
		}
	}
	return B_OK;
}

char *
QuaAsio::CurrentDriver()
{
	static char buf[MAXDRVNAMELEN];
	if (drivers.getCurrentDriverName(buf)) {
		return buf;
	}
	return nullptr;
}

void
QuaAsio::UnloadDriver()
{
	if (!loaded)
		return;
	if (running) {
		ASIOStop();
	}

	for (int i=0; i<input.size(); i++) {
		if (input[i]) {
			delete input[i];
		}
	}
	input.clear();

	for (int i=0; i<output.size(); i++) {
		if (output[i]) {
			delete output[i];
		}
	}
	output.clear();

	ASIODisposeBuffers();
	if (buffers) {
		delete buffers;
		buffers = nullptr;
	}
	drivers.removeCurrentDriver();
	loaded = false;
}

status_t
QuaAsio::LoadDriver(char *name)
{
	if (CurrentDriver() != nullptr && strcmp(CurrentDriver(),name) == 0) {
		return B_OK;
	}
	short found = -1;
	for (short i=0; i<nDrivers; i++) {
		if (strcmp(name, installedDrivers[i]) == 0) {
			found = i;
			break;
		}
	} 
	if (found < 0) {
		cout << "driver "<< name <<" unknown" << endl;
		return -1;
	}
	cout << "Asio: loading driver " << name << endl;
	if (loaded) {
		UnloadDriver();
	}
	if (drivers.loadDriver(const_cast<char*>(name))) {
		ASIOError		err;
		ASIODriverInfo	drvInf;
		int				i;
		loaded = true;

		nInputChannels = nOutputChannels = 0;
		inputLatency = outputLatency = 0;
		bufSize = minBufSize = maxBufSize = preferredBufSize = bufSizeGranularity = 0;
		drvInf.asioVersion = 2;
		drvInf.sysRef = nullptr;
		
		if ((err=ASIOInit(&drvInf)) != ASE_OK) {
// ASE_NotPresent
// ASE_HWMalfunction
// ASE_NoMemory
			cout << "err initializing ..." << endl;
			return err;
		}
		if ((err=ASIOGetChannels(&nInputChannels, &nOutputChannels)) != ASE_OK) {
			cout << "err getting channels ..." << endl;
			return err;
		}
		if ((err=ASIOGetLatencies(&inputLatency, &outputLatency)) != ASE_OK) {
			cout << "err getting latencies ..." << endl;
			return err;
		}
		if ((err=ASIOGetBufferSize(
					&minBufSize, &maxBufSize,
					&preferredBufSize, &bufSizeGranularity)) != ASE_OK) {
			cout << "err getting buf size ..." << endl;
			return err;
		}
		bufSize = preferredBufSize;

		cout << "loaded " << name << ", " << nInputChannels << " ins " << nOutputChannels << " outs " <<  endl;
		cout << "input latence " << inputLatency << " output latency " << outputLatency << endl;
		cout << "min buf "<< minBufSize <<" max buf " << maxBufSize << " pref buf " << preferredBufSize << endl;

		ASIOSampleRate	sampleRate;
		// get the currently selected sample rate
		if((err=ASIOGetSampleRate(&sampleRate)) != ASE_OK) {
			cout << "Can't get sample rate" << endl;
		}
		cout << "ASIOGetSampleRate (sampleRate: " << sampleRate << ")" << endl;
					// Driver does not store it's internal sample rate, so set it to a know one.
					// Usually you should check beforehand, that the selected sample rate is valid
					// with ASIOCanSampleRate().
//					if(ASIOSetSampleRate(44100.0) != ASE_OK) {
//						;
//					}
//						if(ASIOGetSampleRate(&asioDriverInfo->sampleRate) == ASE_OK)
//							printf ("ASIOGetSampleRate (sampleRate: %f);" << endl, asioDriverInfo->sampleRate);

				// check wether the driver requires the ASIOOutputReady() optimization
				// (can be used by the driver to reduce output latency by one block)
		if(ASIOOutputReady() == ASE_OK)
			outputReady = true;
		else
			outputReady = false;
		cout << "Output ready call "  << (outputReady?"needed":"not needed") << endl;

		ASIOChannelInfo info;
		info.isInput = ASIOTrue;
		ASIOError	chanerr = ASE_OK;
// assert ... input and output should both be empty here
		for (i=0; i<nInputChannels; i++) {
			info.channel = i;
			if ((err=ASIOGetChannelInfo(&info)) != ASE_OK) {
				cout << "err retrieving input..." << endl;
				chanerr = err;
				input.push_back(nullptr);
			} else {
				input.push_back( new QuaAudioIn(nullptr, nullptr,
									info.channel, info.channelGroup,
									info.name, info.type, 1));
				cout << "input "<< info.name <<", ch " << info.channel << " grp " << info.channelGroup << " typ " << info.type << endl;
			}
		}
		info.isInput = ASIOFalse;

		for (i=0; i<nOutputChannels; i++) {
			info.channel = i;
			if ((err=ASIOGetChannelInfo(&info)) != ASE_OK) {
				cout << "err retrieving output..." << endl;
				chanerr = err;
				output.push_back(nullptr);
			} else {
				output.push_back(new QuaAudioOut(nullptr, nullptr,
									info.channel, info.channelGroup,
									info.name, info.type, 1));
				cout << "output " << info.name << ", ch " << info.channel << " grp " << info.channelGroup << " typ " << info.type << endl;
			}
		}

		if (chanerr != ASE_OK) {
			return chanerr;
		}

		return B_OK; //AllocateBuffers();
	} else {
		cout << "loaded ok" << endl;
		return ASE_NotPresent;
	}
	return ASE_OK;
}

status_t
QuaAsio::SetBufSize(long bs)
{
	bufSize = bs;
	return AllocateBuffers();
}

status_t
QuaAsio::AllocateBuffers()
{
	cout << "Asio:AllocateBuffers()" << endl;
	ASIOError	err;
	if (running) {
		err = ASIOStop();
		if (err != ASE_OK)
			return err;
	}

	if (buffers) {
		cout << "Asio: disposing old buffers" << endl;
		ASIODisposeBuffers();
		delete buffers;
		buffers = nullptr;
	}
	nActiveInput = nActiveOutput = 0;
	cout << "Asio: checking "<< nInputChannels <<" input channels" << endl;
	for (short i=0; i<nInputChannels; i++) {
		if (input[i] && input[i]->enabled) {
			nActiveInput++;
		}
	}
	cout << "Asio: checking " << nOutputChannels << " output channels" << endl;
	for (short i=0; i<nOutputChannels; i++) {
		if (output[i] && output[i]->enabled) {
			nActiveOutput++;
		}
	}
	if (nActiveInput+nActiveOutput == 0) {
		return B_OK;
	}
	cout << "Asio: allocating new buffers" << endl;
	buffers = new ASIOBufferInfo[nActiveInput+nActiveOutput];
	long ind = 0;
	int i=0;
	while (ind < nActiveInput) {
		buffers[ind].isInput = true;
		buffers[ind].channelNum = i++;
		buffers[ind].buffers[0] = buffers[ind].buffers[1] = nullptr;
		ind++;
	}
	i=0;
	while (ind<nActiveInput+nActiveOutput) {
		buffers[ind].isInput = false;
		buffers[ind].channelNum = i++;
		buffers[ind].buffers[0] = buffers[ind].buffers[1] = nullptr;
		ind++;
	}

	callbacks.asioMessage = &asioMessages;
	callbacks.bufferSwitch = &bufferSwitch;
	callbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;
	callbacks.sampleRateDidChange = &sampleRateChanged;

	cout << "Asio: creating "<< (nActiveInput + nActiveOutput) <<" buffers, size " << bufSize << endl;
	err = ASIOCreateBuffers(buffers, nActiveInput+nActiveOutput, bufSize, &callbacks); 
	if (err == ASE_OK && (err=ASIOGetLatencies(&inputLatency, &outputLatency)) != ASE_OK) {
		cout <<"err getting latencies ..." << endl;
		return err;
	}
	cout << "Asio: new buffer set allocated. alloc err = " << err << " latencies " << inputLatency << "" << outputLatency << "" << endl;
	if (err == ASE_OK && running) {
		err = ASIOStart();
		cout << "Asio: starting ... err " << err << endl;
	}
	return err;
}

QuaAudioIn *
QuaAsio::EnableInput(Qua *q, long id)
{
	if (id >= 0 && id < nInputChannels && input[id]) {
		QuaAudioIn	*ip = input[id];
		ip->uberQua = q;
		ip->enabled = true;
		if (ip->data == nullptr) {
			ip->data = new FloatQueue(16*bufSize*ByteSize(ip->sampleFormat));
		}
		return ip;
	}
	return nullptr;
}


QuaAudioOut *
QuaAsio::EnableOutput(Qua *q, long id)
{
	if (id >= 0 && id < nOutputChannels && output[id]) {
		QuaAudioOut	*op = output[id];
		op->uberQua = q;
		op->enabled = true;
		if (op->outbuf == nullptr) {
			op->outbuf = new float[bufSize];
			cout << "enable "<< bufSize <<" "<< op->outbuf <<"" << endl;
		}
		return op;
	}
	return nullptr;
}

bool
QuaAsio::DisableInput(long id)
{
	if (id >= 0 && id < nInputChannels) {
		input[id]->enabled = false;
		input[id]->uberQua = nullptr;
	}
	return true;
}


bool
QuaAsio::DisableOutput(long id)
{
	if (id >= 0 && id < nOutputChannels) {
		output[id]->enabled = false;
		output[id]->uberQua = nullptr;
	}
	return true;
}

status_t
QuaAsio::Start()
{
	running = true;
	return ASIOStart();
}

status_t
QuaAsio::Stop()
{
	running = false;
	return ASIOStop();
}

void 
QuaAsio::bufferSwitch(long doubleBufferIndex, ASIOBool directProcess)
{
	short i;
	if (debug_asio) {
		cout << "ASIO:bufferSwitch called "<< doubleBufferIndex <<" " << directProcess << "" << endl;
	}
	for (i=0; i<nActiveInput; i++) {
		QuaAudioIn	*ip = input[buffers[i].channelNum];
		if (ip->enabled) {
			void		*buf = buffers[i].buffers[doubleBufferIndex];
			float		*d = ip->data->InPtr();
			switch (ip->sampleFormat) {
////////////////////// PPC/Motorola (MSB) Byte order /////////////////////				
				case ASIOSTInt16MSB: {
					break;
				}
				case ASIOSTInt24MSB: {		// used for 20 bits as well
					break;
				}
				case ASIOSTInt32MSB: {
					break;
				}
				case ASIOSTFloat32MSB: {		// IEEE 754 32 bit float
					break;
				}
				case ASIOSTFloat64MSB: {		// IEEE 754 64 bit double float
					break;
				}

				case ASIOSTInt32MSB16: {		// 32 bit data with 18 bit alignment
					break;
				}
				case ASIOSTInt32MSB18: {	// 32 bit data with 18 bit alignment
					break;
				}	
				case ASIOSTInt32MSB20: {	// 32 bit data with 20 bit alignment
					break;
				}	
				case ASIOSTInt32MSB24: {	// 32 bit data with 24 bit alignment
					break;
				}	
////////////////////// Intel (LSB) Byte order /////////////////////				
				case ASIOSTInt16LSB: {
					int16	*s = (int16 *)buf;
					for (short i=0; i<bufSize; i++) {
						*d++ = ((float)(*s++))/ASIONormalize16bit;
					}
					break;
				}
				case ASIOSTInt24LSB: {		// used for 20 bits as well
					char	*s = (char *)buf;
					for (short i=0; i<bufSize; i++) {
						int si = *s++;
						si = si |((*s++)<< 8);
						si = si |((*s++)<< 8);
						*(d++) = ((float)si)/ASIONormalize24bit;
					}
					break;
				}
				case ASIOSTInt32LSB: {
					int32	*s = (int32 *)buf;
					for (short i=0; i<bufSize; i++) {
						*d++ = (*s++)/ASIONormalize32bit;
					}
					break;
				}
				case ASIOSTFloat32LSB: {		// IEEE 754 32 bit float, as found on Intel x86 architecture
					float	*s = (float *)buf;
					for (short i=0; i<bufSize; i++) {
						*d++ = *s++;
					}
					break;
				}
				case ASIOSTFloat64LSB: {		// IEEE 754 64 bit double float, as found on Intel x86 architecture
					double	*s = (double *)buf;
					for (short i=0; i<bufSize; i++) {
						*d++ = *s++;
					}
					break;
				} 

				case ASIOSTInt32LSB16: {		// 32 bit data with 18 bit alignment
					break;
				}
				case ASIOSTInt32LSB18: {		// 32 bit data with 18 bit alignment
					break;
				}
				case ASIOSTInt32LSB20: {		// 32 bit data with 20 bit alignment
					break;
				}
				case ASIOSTInt32LSB24: {		// 32 bit data with 24 bit alignment
					break;
				}
			}
		}
	}
	if (debug_asio >= 2)
		cout << "ASIO:bufferSwitch sorting inputs" << endl;
	//uberQua->objectsBlockStack.ReadLock();
	for (short i=0; i<audio.channels.size(); i++) {
		Channel	*c = audio.channels[i];
		bool	recording = (c->recordState == RECORD_ING);

		if (c->audioThru || recording) {
			c->receive(bufSize);
		}
	}
	for (short i=0; i<nActiveInput; i++) {
		QuaAudioIn	*yp = input[buffers[i].channelNum];
		if (yp && yp->data) yp->data->Take(bufSize*yp->nChannel);
	}
	if (debug_asio >= 2)
		cout << "ASIO:bufferSwitch calling generate" << endl;
	audio.Generate(bufSize);

	if (debug_asio >= 2)
		cout << "ASIO:bufferSwitch copying to asio buffs" << endl;
	//uberQua->objectsBlockStack.ReadUnlock();
	short ind = nActiveInput;
	for (short i=0; i<nActiveOutput; i++) {
		QuaAudioOut	*op = output[buffers[ind].channelNum];
		if (op->enabled) {
			void		*buf = buffers[ind].buffers[doubleBufferIndex];
			float		*s = op->outbuf+op->offset;
			long		nFrames = bufSize / op->nChannel;
			switch (op->sampleFormat) {
////////////////////// PPC/Motorola (MSB) Byte order /////////////////////				
				case ASIOSTInt16MSB: {
					break;
				}
				case ASIOSTInt24MSB: {		// used for 20 bits as well
					break;
				}
				case ASIOSTInt32MSB: {
					break;
				}
				case ASIOSTFloat32MSB: {		// IEEE 754 32 bit float
					break;
				}
				case ASIOSTFloat64MSB: {		// IEEE 754 64 bit double float
					break;
				}

				case ASIOSTInt32MSB16: {		// 32 bit data with 18 bit alignment
					break;
				}
				case ASIOSTInt32MSB18: {	// 32 bit data with 18 bit alignment
					break;
				}	
				case ASIOSTInt32MSB20: {	// 32 bit data with 20 bit alignment
					break;
				}	
				case ASIOSTInt32MSB24: {	// 32 bit data with 24 bit alignment
					break;
			}	
////////////////////// Intel (LSB) Byte order /////////////////////				
				case ASIOSTInt16LSB: {
					int16	*d = (int16 *)buf;
					for (short j=0; j<bufSize; j++) {
						*d++ = (*s++)*ASIONormalize16bit;
					}
					break;
				}
				case ASIOSTInt24LSB: {		// used for 20 bits as well
					char	*d = (char *)buf;
					for (short j=0; j<bufSize; j++) {
						int si = (*s++)*ASIONormalize24bit;
						*(d++) = (char)(si&0xff);
						*(d++) = (char)((si&0xff00)>>8);
						*(d++) = (char)((si&0xff0000)>>16);
					}
					break;
				}
				case ASIOSTInt32LSB: {
					int32	*d = (int32 *)buf;
					for (short j=0; j<bufSize; j++) {
						*d++ = (*s++)*ASIONormalize32bit;
					}
					break;
				}
				case ASIOSTFloat32LSB: {		// IEEE 754 32 bit float, as found on Intel x86 architecture
					float	*d = (float *)buf;
					for (short j=0; j<bufSize; j++) {
						*d++ = *s++;
					}
					break;
				}
				case ASIOSTFloat64LSB: {		// IEEE 754 64 bit double float, as found on Intel x86 architecture
					double	*d = (double *)buf;
					for (short j=0; j<bufSize; j++) {
						*d++ = *s++;
					}
					break;
				} 

				case ASIOSTInt32LSB16: {		// 32 bit data with 18 bit alignment
					break;
				}
				case ASIOSTInt32LSB18: {		// 32 bit data with 18 bit alignment
					break;
				}
				case ASIOSTInt32LSB20: {		// 32 bit data with 20 bit alignment
					break;
				}
				case ASIOSTInt32LSB24: {		// 32 bit data with 24 bit alignment
					break;
				}
			}
		}
		ind++;
	}

	if (outputReady) {
		ASIOOutputReady();
	}
/*
	if (uberQua->timingType == QUA_TIME_ASIO) {
		uberQua->theClock->Tick();
	}
*/
	if (debug_asio >= 2)
		cout << "ASIO:bufferSwitch done" << endl;
}

ASIOTime*
QuaAsio::bufferSwitchTimeInfo(
				ASIOTime* params,
				long doubleBufferIndex,
				ASIOBool directProcess)
{
	cout << "ASIO:bufferSwitchTimeInfo" << endl;
	bufferSwitch(doubleBufferIndex, directProcess);
	return nullptr;
}

void
QuaAsio::sampleRateChanged(ASIOSampleRate sRate)
{
        // do whatever you need to do if the sample rate changed
        // usually this only happens during external sync.
        // Audio processing is not stopped by the driver, actual sample rate
        // might not have even changed, maybe only the sample rate status of an
        // AES/EBU or S/PDIF digital input at the audio device.
        // You might have to update time/sample related conversion routines, etc.
}

long
QuaAsio::asioMessages(long selector, long value, void* message, double* opt)
{
	// currently the parameters "value", "message" and "opt" are not used.
	long ret = 0;
	switch(selector) {
		case kAsioSelectorSupported:
				if(value == kAsioResetRequest
				|| value == kAsioEngineVersion
				|| value == kAsioResyncRequest
				|| value == kAsioLatenciesChanged
				// the following three were added for ASIO 2.0, you don't necessarily have to support them
				|| value == kAsioSupportsTimeInfo
				|| value == kAsioSupportsTimeCode
				|| value == kAsioSupportsInputMonitor)
						ret = 1L;
				cout << "Asio::asioMessages(kAsioSelectorSupported)" << endl;
				break;
			    
		case kAsioBufferSizeChange:
				cout << "Asio::asioMessages(kAsioBufferSizeChange)" << endl;
				break;
			    
		case kAsioResetRequest:
				// defer the task and perform the reset of the driver during the next "safe" situation
				// You cannot reset the driver right now, as this code is called from the driver.
				// Reset the driver is done by completely destruct is. I.e. ASIOStop(), ASIODisposeBuffers(), Destruction
				// Afterwards you initialize the driver again.
//				asioDriverInfo.stopped;  // In this sample the processing will just stop
				cout << "Asio::asioMessages(kAsioResetRequest) " << endl;
				ret = 1L;
				break;
		case kAsioResyncRequest:
				// This informs the application, that the driver encountered some non fatal data loss.
				// It is used for synchronization purposes of different media.
				// Added mainly to work around the Win16Mutex problems in Windows 95/98 with the
				// Windows Multimedia system, which could loose data because the Mutex was hold too long
				// by another thread.
				// However a driver can issue it in other situations, too.
				cout << "Asio::asioMessages(kAsioResyncRequest) " << endl;
				ret = 1L;
				break;
		case kAsioLatenciesChanged:
				// This will inform the host application that the drivers were latencies changed.
				// Beware, it this does not mean that the buffer sizes have changed!
				// You might need to update internal delay data.
				ret = 1L;
				cout << "Asio::asioMessages(kAsioLatenciesChanged) " << endl;
				break;
		case kAsioEngineVersion:
				// return the supported ASIO version of the host application
				// If a host applications does not implement this selector, ASIO 1.0 is assumed
				// by the driver
				cout << "Asio::asioMessages(kAsioEngineVersion) " << endl;
				ret = 2L;
				break;
		case kAsioSupportsTimeInfo:
				// informs the driver wether the asioCallbacks.bufferSwitchTimeInfo() callback
				// is supported.
				// For compatibility with ASIO 1.0 drivers the host application should always support
				// the "old" bufferSwitch method, too.
				cout << "Asio::asioMessages(kAsioSupportsTimeInfo) " << endl;
				ret = 0;
				break;
		case kAsioSupportsTimeCode:
				// informs the driver wether application is interested in time code info.
				// If an application does not need to know about time code, the driver has less work
				// to do.
				cout << "Asio::asioMessages(kAsioSupportsTimeCode) " << endl;
				ret = 0;
				break;
		default:
				cout << "Asio::asioMessages(unimp %d) " << endl;
			break;
	}
	return ret;
}

char *
QuaAsio::ErrorString(ASIOError err)
{
	switch(err) {
	case ASE_OK: return "asio ok";             // This value will be returned whenever the call succeeded
	case ASE_SUCCESS: return "asio success";	// unique success return value for ASIOFuture calls
	case ASE_NotPresent: return "asio not present"; // hardware input or output is not present or available
	case ASE_HWMalfunction: return "asio hw malfunction";      // hardware is malfunctioning (can be returned by any ASIO function)
	case ASE_InvalidParameter: return "asio invalid parameter";   // input parameter invalid
	case ASE_InvalidMode: return "asio invalid mode";        // hardware is in a bad mode or used in a bad mode
	case ASE_SPNotAdvancing: return "asio not advancing";     // hardware is not running when sample position is inquired
	case ASE_NoClock: return "asio no clock";            // sample clock or rate cannot be determined or is not present
	case ASE_NoMemory: return "asio no memory";            // not enough memory for completing the request
	}
	return "unknown asio error";
}

short
QuaAsio::ByteSize(long t)
{
	switch (t) {
	case ASIOSTInt16LSB:
	case ASIOSTInt16MSB: return 2;
	case ASIOSTInt24LSB:
	case ASIOSTInt24MSB: return 3;
	case ASIOSTFloat32MSB:
	case ASIOSTInt32LSB:
	case ASIOSTFloat32LSB:
	case ASIOSTInt32LSB18:
	case ASIOSTInt32LSB20:
	case ASIOSTInt32LSB24:
	case ASIOSTInt32MSB16:
	case ASIOSTInt32MSB18:
	case ASIOSTInt32MSB20:
	case ASIOSTInt32MSB24:
	case ASIOSTInt32MSB: return 4;
	case ASIOSTFloat64LSB:
	case ASIOSTFloat64MSB: return 8;
	default: return 4;
	}
}

#endif
