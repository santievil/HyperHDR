/* AmlogicGrabber.cpp
*
*  MIT License
*
*  Copyright (c) 2020-2025 awawa-dev
*
*  Project homesite: https://github.com/awawa-dev/HyperHDR
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.

*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE.
 */

#include <iostream>
#include <sys/ioctl.h>
#include <stdexcept>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include <base/HyperHdrInstance.h>
#include <base/AccessManager.h>

#include <QDirIterator>
#include <QFileInfo>
#include <QCoreApplication>
#include <QByteArray>

#include <grabber/linux/amlogic/AmlogicGrabber.h>
#include <image/MemoryBuffer.h>

// util
#include <utils/GlobalSignals.h>


namespace {
	const int  AMVIDEOCAP_WAIT_MAX_MS = 40;
	const char DEFAULT_VIDEO_DEVICE[] = "/dev/amvideo";
	const char DEFAULT_CAPTURE_DEVICE[] = "/dev/amvideocap0";
}


AmlogicGrabber::AmlogicGrabber(const QString& device, const QString& configurationPath)
	: Grabber(configurationPath, "AMLOGIC_SYSTEM:" + device.left(14))
	, _configurationPath(configurationPath)
	, _semaphore(1)
	, _handle(-1)
{
	resetVariables();

	_timer.setTimerType(Qt::PreciseTimer);
	connect(&_timer, &QTimer::timeout, this, &AmlogicGrabber::grabFrame);
	connect(GlobalSignals::getInstance(), &GlobalSignals::SignalSetLut, this, &AmlogicGrabber::signalSetLutHandler, Qt::BlockingQueuedConnection);
	getDevices();
}

void AmlogicGrabber::setAutoToneMappingAML(bool enabled)
{
    _autoToneMappingAML = enabled;
    Info(_log, "AmlogicGrabber AutoToneMap = {}", _autoToneMappingAML  ? "ON" : "OFF");
}

void AmlogicGrabber::resetVariables()
{
	_amlFrame.releaseMemory();
	_lastValidFrame.releaseMemory();
	_captureDev = -1;
	_videoDev = -1;
	_usingAmlogic = false;
	_messageShow = false;
	_currentHDRState = false;
}

bool AmlogicGrabber::getAspectRatio(int& arW, int& arH)
{
    QFile f("/sys/class/display/mode");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Debug(_log, "Cant open display mode status");
        return false;
    }

    QString m = f.readAll().trimmed();
    f.close();

    int w = 0, h = 0;

    if (m.contains("x"))
        sscanf(m.toStdString().c_str(), "%dx%d", &w, &h);
    else
        sscanf(m.toStdString().c_str(), "%dp", &h), w = h * 16 / 9;

    int a = w, b = h;
    while (b) { int t = b; b = a % b; a = t; }

    arW = w / a;
    arH = h / a;
    return true;
}

bool AmlogicGrabber::checkKodiHDRStatus()
{
	 const QString hdrStatusPath = "/sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status";
    QFile file(hdrStatusPath);
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Debug(_log, "Cant open hdmi_hdr_status");
        return false;
    }
    
    QString status = file.readAll().trimmed();
    file.close();
    
    bool isHDR = !status.isEmpty() && !status.startsWith("SDR", Qt::CaseInsensitive);
    
    if (isHDR)
    {
        Info(_log, "HDR detected: {}", status.toStdString());
    }
    else
    {
        Debug(_log, "SDR mode active");
    }
    
    //return isHDR;
	return true;
}

QString AmlogicGrabber::GetSharedLut()
{
#ifdef __APPLE__
	QString ret = QString("%1%2").arg(QCoreApplication::applicationDirPath()).arg("/../lut");
	QFileInfo info(ret);
	ret = info.absoluteFilePath();
	return ret;
#else
	return QCoreApplication::applicationDirPath();
#endif
}

void AmlogicGrabber::loadLutFile()
{
	QString fileName1 = QString("%1%2").arg(_configurationPath).arg("/flat_lut_lin_tables.3d");
	QString fileName2 = QString("%1%2").arg(_configurationPath).arg("/lut_lin_tables.3d");
	QString fileName3 = QString("%1%2").arg(GetSharedLut()).arg("/lut_lin_tables.3d");
	QList<QString> files({ fileName1, fileName2, fileName3 });

#ifdef __linux__
	QString fileName4 = QString("/usr/share/hyperhdr/lut/lut_lin_tables.3d");

	files.append(fileName4);
#endif
	
	if (!_userLutFile.isEmpty())
	{
		#ifdef __linux__
			QString userFileBin = QString("%1/%2").arg(GetSharedLut()).arg(_userLutFile);
			files.prepend(userFileBin);
			Info(_log, "Adding user LUT file linux for searching: {:s}", (userFileBin));
		#endif

		QString userFile = QString("%1/%2").arg(_configurationPath).arg(_userLutFile);
		files.prepend(userFile);
		Info(_log, "Adding user LUT file for searching: {:s}", (userFile));
	}
	LutLoader::loadLutFile(_log, PixelFormat::RGB24, files);
}

void AmlogicGrabber::setHdrToneMappingEnabled(int mode)
{
	if (_hdrToneMappingEnabled != mode)
	{
		_hdrToneMappingEnabled = mode;
		//if (!_lutBufferInit)
		loadLutFile();
	}
}

AmlogicGrabber::~AmlogicGrabber()
{
	uninit();
}

void AmlogicGrabber::uninit()
{
	if (_initialized)
	{
		stop();
		disconnect(GlobalSignals::getInstance(), &GlobalSignals::SignalSetLut, this, &AmlogicGrabber::signalSetLutHandler);
		Debug(_log, "Uninit grabber: {:s}", (_deviceName));
	}

	_initialized = false;
}

bool AmlogicGrabber::init()
{
	Debug(_log, "init");


	if (!_initialized)
	{
		QString foundDevice = "";
		bool    autoDiscovery = (QString::compare(_deviceName, Grabber::AUTO_SETTING, Qt::CaseInsensitive) == 0);

		enumerateDevices(true);


		if (!autoDiscovery && !_deviceProperties.contains(_deviceName))
		{
			Debug(_log, "Device {:s} is not available. Changing to auto.", (_deviceName));
			autoDiscovery = true;
		}

		if (autoDiscovery)
		{
			Debug(_log, "Forcing auto discovery device");
			if (!_deviceProperties.isEmpty())
			{
				foundDevice = _deviceProperties.firstKey();
				_deviceName = foundDevice;
				Debug(_log, "Auto discovery set to {:s}", (_deviceName));
			}
		}
		else
			foundDevice = _deviceName;

		if (foundDevice.isNull() || foundDevice.isEmpty() || !_deviceProperties.contains(foundDevice))
		{
			Error(_log, "Could not find any capture device.");
			return false;
		}


		Info(_log, "*************************************************************************************************");
		Info(_log, "Starting FrameBuffer grabber. Selected: '{:s}' ({:d}) max width: {:d} ({:d}) @ {:d} fps", (foundDevice), _deviceProperties[foundDevice].valid.first().input, _width, _height, _fps);
		Info(_log, "*************************************************************************************************");

		QByteArray foundDeviceUtf8 = foundDevice.toUtf8();
		_handle = open(foundDeviceUtf8.constData(), O_RDONLY);
		if (_handle < 0)
		{
			Error(_log, "Could not open the framebuffer device: '{:s}'. Reason: {:s} ({:d})", (foundDevice), std::strerror(errno), errno);
		}
		else
		{
			struct fb_var_screeninfo scr;

			if (ioctl(_handle, FBIOGET_VSCREENINFO, &scr) == 0)
			{
				if (scr.bits_per_pixel == 16 || scr.bits_per_pixel == 24 || scr.bits_per_pixel == 32)
				{
					_actualDeviceName = foundDevice;
					Info(_log, "Device '{:s}' is using currently {:d}x{:d}x{:d} resolution.", (_actualDeviceName), scr.xres, scr.yres, scr.bits_per_pixel);
					_initialized = true;
				}
				else
				{
					Error(_log, "Unsupported {:d}x{:d}x{:d} mode for '{:s}' device.", scr.xres, scr.yres, scr.bits_per_pixel, (foundDevice));
					close(_handle);
					_handle = -1;
				}
			}
			else
			{
				Error(_log, "Could not get the framebuffer dimension for '{:s}' device. Reason: {:s} ({:d})", (foundDevice), std::strerror(errno), errno);
				close(_handle);
				_handle = -1;
			}
		}
	}

	return _initialized;
}


void AmlogicGrabber::getDevices()
{
	enumerateDevices(false);
}

bool AmlogicGrabber::isActivated()
{
	return !_deviceProperties.isEmpty();
}

void AmlogicGrabber::enumerateDevices(bool silent)
{
	_deviceProperties.clear();

	for (int i = 0; i <= 16; i++)
	{
		QString path = QString("/dev/fb%1").arg(i);
		if (QFileInfo(path).exists())
		{
			DeviceProperties properties;
			DevicePropertiesItem dpi;

			dpi.input = i;
			properties.valid.append(dpi);

			_deviceProperties.insert(path, properties);

			if (!silent)
				Info(_log, "Found FrameBuffer device: {:s}", (path));
		}
	}
}

bool AmlogicGrabber::start()
{
	try
	{
		if (init())
		{
			_timer.setInterval(1000 / _fps);
			_timer.start();
			Info(_log, "Started");
			return true;
		}
	}
	catch (std::exception& e)
	{
		Error(_log, "Start failed ({:s})", e.what());
	}

	return false;
}

void AmlogicGrabber::stop()
{
	if (_initialized)
	{
		_semaphore.acquire();
		_timer.stop();

		if (_handle >= 0)
		{
			close(_handle);
			_handle = -1;
		}
		_initialized = false;

		resetVariables();

		_semaphore.release();
		Info(_log, "Stopped");
	}
}

void AmlogicGrabber::grabFrame()
{
	bool stopNow = false;

	if (_semaphore.tryAcquire()) {
		try {
			if (_initialized) {				
				bool isVideoPlaying = isVideoPlayingAML();

				// Change capture device when needed
				if (isVideoPlaying != _usingAmlogic) {
					if (isVideoPlaying) {
						if (!_usingAmlogic)	Info(_log, "Change to Amlogic");						
						_usingAmlogic = initAmlogic();
					}
					else {
						Info(_log, "Change to Framebuffer");
						if (_lastValidFrame.size() > 0)
						{
							_lastValidFrame.releaseMemory();
						}
						if (_amlFrame.size() > 0)
						{
							_amlFrame.releaseMemory();
						}
						_usingAmlogic = !stopAmlogic();
					}
					_messageShow = false;
				}

				// Capture framel
				if (_usingAmlogic)
				{
					if (!_messageShow)
					{
						Info(_log, "Grabbing Amlogic");
						_messageShow = true;
						if (_autoToneMappingAML)
						{
							_currentHDRState = checkKodiHDRStatus();
							
							if (_currentHDRState)                 		
								setHdrToneMappingEnabled(1);
							else
								setHdrToneMappingEnabled(0);
						}else
							setHdrToneMappingEnabled(0);
							
						int w, h;
							if (getAspectRatio(w, h))
								 _height = (_width * h) / w;
					}
					grabFrameAmlogic();
				}
				else {
					if (!_messageShow)
					{
						Info(_log, "Grabbing Framebuffer");
						_messageShow = true;
						_currentHDRState = false;
						setHdrToneMappingEnabled(0);
					}
					stopNow = grabFrameFramebuffer();
					if (stopNow)
					{
						uninit();
					}
				}
			}
		}
		catch (const std::exception& e) {
			Error(_log, "Error capturing frame: {:s}", e.what());
		}

		_semaphore.release();

	}
}

bool AmlogicGrabber::grabFrameFramebuffer()
{
	struct fb_var_screeninfo scr;
	bool isStillActive = false;
	if (ioctl(_handle, FBIOGET_VSCREENINFO, &scr) == 0)
	{
		isStillActive = true;
	}
	else
	{
		Warning(_log, "The handle is lost. Trying to restart the driver.");
		Info(_log, "The handle is lost. Trying to restart the driver."); //quitar

		close(_handle);

		QByteArray actualDeviceNameUtf8 = _actualDeviceName.toUtf8();
		_handle = open(actualDeviceNameUtf8.constData(), O_RDONLY);

		if (_handle >= 0 && ioctl(_handle, FBIOGET_VSCREENINFO, &scr) == 0)
		{
			isStillActive = true;
		}
	}

	if (isStillActive)
	{
		_actualWidth = scr.xres;
		_actualHeight = scr.yres;

		if (scr.bits_per_pixel == 16 || scr.bits_per_pixel == 24 || scr.bits_per_pixel == 32)
		{
			struct fb_fix_screeninfo format;

			if (ioctl(_handle, FBIOGET_FSCREENINFO, &format) >= 0)
			{
				uint8_t* memHandle = static_cast<uint8_t*>(mmap(nullptr, format.smem_len, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, _handle, 0));

				if (memHandle == MAP_FAILED)
				{
					Error(_log, "Could not map the framebuffer memory.");
					return true;
				}
				else
				{
					if (scr.bits_per_pixel == 32)
						processSystemFrameBGRA(memHandle, format.line_length);
					else if (scr.bits_per_pixel == 24)
						processSystemFrameBGR(memHandle, format.line_length);
					else if (scr.bits_per_pixel == 16)
						processSystemFrameBGR16(memHandle, format.line_length);

					munmap(memHandle, format.smem_len);
					return false;
				}
			}
			else
			{
				Error(_log, "Could not read the framebuffer properties.");
				return true;
			}
		}
		return true;
	}
	else
	{
		Error(_log, "Could not read the framebuffer dimension.");
		return true;
	}
}


void AmlogicGrabber::setCropping(unsigned cropLeft, unsigned cropRight, unsigned cropTop, unsigned cropBottom)
{
	_cropLeft = cropLeft;
	_cropRight = cropRight;
	_cropTop = cropTop;
	_cropBottom = cropBottom;
}


bool AmlogicGrabber::grabFrameAmlogic()
{
	long r1 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH, _width);
	long r2 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT, _height);
	long r3 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS, CAP_FLAG_AT_END);
	long r4 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_WAIT_MAX_MS, AMVIDEOCAP_WAIT_MAX_MS);

	if (r1 < 0 || r2 < 0 || r3 < 0 || r4 < 0 || _height == 0 || _width == 0)
	{
		Error(_log, "Failed to configure Amlogic capture device");
		return false;
	}
	else
	{
		//Solo para testeo
		//_width = 1920;
		//_height = 1080;
		_actualWidth = _width;
		_actualHeight = _height;
		int linelen = ((_width + 31) & ~31) * 3;
		size_t _bytesToRead = linelen * _height;

		_amlFrame.resize(_bytesToRead);

		if (_amlFrame.size() == 0) {
			Error(_log, "Malloc _bytesToRead %zu failed\n", _bytesToRead);
			return false;
		}

		ssize_t bytesRead = pread(_captureDev, _amlFrame.data(), _bytesToRead, 0);

		if (bytesRead < 0 && !EAGAIN && errno > 0)
		{
			Error(_log, "Capture frame failed  failed - Retrying. Error [{:d}] - {:s}", errno, strerror(errno));			
			_amlFrame.releaseMemory();
			return false;
		}
		else
		{
			if (bytesRead != -1 && static_cast<ssize_t>(_bytesToRead) != bytesRead)
			{
				Error(_log, "Capture failed to grab entire image [bytesToRead({:d}) != bytesRead({:d})]", _bytesToRead, bytesRead);
				_amlFrame.releaseMemory();
				return false;
			}
			else {
				if (bytesRead > 0) //Only if capture has data to avoid crash on processSystemFrameBGR
				{					
					_lastValidFrame.resize(_bytesToRead);
					if (_lastValidFrame.size() > 0)
					{
						memcpy(_lastValidFrame.data(), _amlFrame.data(), _bytesToRead);
					}

					processSystemFrameBGR(static_cast<uint8_t*>(_amlFrame.data()), linelen);
					return true;
				}
				else
				{					
					if (_lastValidFrame.size() > 0)
					{					
						processSystemFrameBGR(_lastValidFrame.data(), linelen);
						return true;
					}
	
					return false;
				}
			}
		}
	}
	return true;
}

bool AmlogicGrabber::initAmlogic()
{
	Info(_log, "Starting Amlogic capture device...");
	try {
		_captureDev = open(DEFAULT_CAPTURE_DEVICE, O_RDWR);
		if (_captureDev < 0) {
			Error(_log, "Failed to open Amlogic capture device: {:s}", strerror(errno));
			return false;
		}

		Info(_log, "Amlogic capture device opened.");
		return true;
	}
	catch (const std::exception& e) {
		Error(_log, "Failed to open Amlogic capture device: {:s}", e.what());
		return false;
	}
}

bool AmlogicGrabber::stopAmlogic()
{
	Info(_log, "Stopping Amlogic capture device...");
	try {
		if (_captureDev >= 0) closeDeviceAML(_captureDev);
		if (_videoDev >= 0) closeDeviceAML(_videoDev);
		if (_captureDev == -1 && _videoDev == -1) {
			Info(_log, "Amlogic capture device stopped.");
			return true;
		}
		return false;

	}
	catch (const std::exception& e) {
		Error(_log, "Failed to stop Amlogic capture device: {:s}", e.what());
		return false;
	}
}

void AmlogicGrabber::closeDeviceAML(int& fd)
{
	if (fd >= 0)
	{
		::close(fd);
		fd = -1;
	}
}

bool AmlogicGrabber::openDeviceAML(int& fd, const char* dev)
{
	if (fd < 0)
	{
		fd = ::open(dev, O_RDWR);
		if (fd < 0)
		{
			return false;
		}
	}
	return true;
}

bool AmlogicGrabber::isVideoPlayingAML()
{
	if (QFile::exists(DEFAULT_VIDEO_DEVICE))
	{
		int videoDisabled = 1;
		if (!openDeviceAML(_videoDev, DEFAULT_VIDEO_DEVICE))
		{
			Error(_log, "Failed to open video device({:s}): {:d} - {:s}", DEFAULT_VIDEO_DEVICE, errno, strerror(errno));
		}
		else
		{
			// Check the video disabled flag
			if (ioctl(_videoDev, AMSTREAM_IOC_GET_VIDEO_DISABLE, &videoDisabled) < 0)
			{
				Error(_log, "Failed to retrieve video state from device: {:d} - {:s}", errno, strerror(errno));
				closeDeviceAML(_videoDev);
			}
			else
			{
				if (videoDisabled == 0)
				{
					return true;
				}
			}
		}

	}
	return false;
}

void AmlogicGrabber::signalSetLutHandler(MemoryBuffer<uint8_t>* lut)
{
	if (!lut){
		Error(_log, "LUT not available");
		return;
	}
        
	if (_lut.size() != lut->size())
        _lut.resize(lut->size());

	if (lut != nullptr && _lut.size() >= lut->size())
	{
		memcpy(_lut.data(), lut->data(), lut->size());
		_lutBufferInit = true;
		_hdrToneMappingEnabled = 1;
		Info(_log, "Amlogic The byte array loaded into LUT");
		//Info(_log, "AmlogicGrabber: Checking state... Is Calibrating: {}", isCalibratingLut() ? "Yes" : "No");

	}
	else
		Error(_log, "Vengo de Amlogic. Could not set LUT: current size = {:d}, incoming size = {:d}", _lut.size(), (lut != nullptr) ? lut->size() : 0);
}
