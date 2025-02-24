/* FrameBufGrabber.cpp
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

#include <grabber/linux/framebuffer/FrameBufGrabber.h>

//---- captura imagenes
#include <ctime>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <filesystem>

#define MAX_IMAGES 10



const int  AMVIDEOCAP_WAIT_MAX_MS = 40;
const char DEFAULT_VIDEO_DEVICE[] = "/dev/amvideo";
const char DEFAULT_CAPTURE_DEVICE[] = "/dev/amvideocap0";
typedef int64_t LONG_PTR, * PLONG_PTR;
typedef LONG_PTR SSIZE_T, * PSSIZE_T;


std::time_t FrameBufGrabber::lastCaptureTime = 0;
std::vector<std::string> FrameBufGrabber::savedImages;

char* buf;
int bufsize;

FrameBufGrabber::FrameBufGrabber(const QString& device, const QString& configurationPath)
	: Grabber(configurationPath, "FRAMEBUFFER_SYSTEM:" + device.left(14))
	, _configurationPath(configurationPath)
	, _semaphore(1)
	, _handle(-1)
{
	_timer.setTimerType(Qt::PreciseTimer);
	connect(&_timer, &QTimer::timeout, this, &FrameBufGrabber::grabFrame);

	getDevices();

	//For files generation
	currentTime = std::time(nullptr);
	lastCaptureTime = currentTime;
}

QString FrameBufGrabber::GetSharedLut()
{
	return "";
}

void FrameBufGrabber::loadLutFile(PixelFormat color)
{
}

void FrameBufGrabber::setHdrToneMappingEnabled(int mode)
{
}

FrameBufGrabber::~FrameBufGrabber()
{
	uninit();
}

void FrameBufGrabber::uninit()
{
	// stop if the grabber was not stopped
	if (_initialized)
	{
		stop();
		Debug(_log, "Uninit grabber: %s", QSTRING_CSTR(_deviceName));
	}

	_initialized = false;
}

bool FrameBufGrabber::init()
{
	Debug(_log, "init");


	if (!_initialized)
	{
		QString foundDevice = "";
		bool    autoDiscovery = (QString::compare(_deviceName, Grabber::AUTO_SETTING, Qt::CaseInsensitive) == 0);

		enumerateDevices(true);


		if (!autoDiscovery && !_deviceProperties.contains(_deviceName))
		{
			Debug(_log, "Device %s is not available. Changing to auto.", QSTRING_CSTR(_deviceName));
			autoDiscovery = true;
		}

		if (autoDiscovery)
		{
			Debug(_log, "Forcing auto discovery device");
			if (!_deviceProperties.isEmpty())
			{
				foundDevice = _deviceProperties.firstKey();
				_deviceName = foundDevice;
				Debug(_log, "Auto discovery set to %s", QSTRING_CSTR(_deviceName));
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
		Info(_log, "Starting FrameBuffer grabber. Selected: '%s' (%i) max width: %d (%d) @ %d fps", QSTRING_CSTR(foundDevice), _deviceProperties[foundDevice].valid.first().input, _width, _height, _fps);
		Info(_log, "*************************************************************************************************");

		_handle = open(QSTRING_CSTR(foundDevice), O_RDONLY);
		if (_handle < 0)
		{
			Error(_log, "Could not open the framebuffer device: '%s'. Reason: %s (%i)", QSTRING_CSTR(foundDevice), std::strerror(errno), errno);
		}
		else
		{
			struct fb_var_screeninfo scr;

			if (ioctl(_handle, FBIOGET_VSCREENINFO, &scr) == 0)
			{
				if (scr.bits_per_pixel == 16 || scr.bits_per_pixel == 24 || scr.bits_per_pixel == 32)
				{
					_actualDeviceName = foundDevice;
					Info(_log, "Device '%s' is using currently %ix%ix%i resolution.", QSTRING_CSTR(_actualDeviceName), scr.xres, scr.yres, scr.bits_per_pixel);
					_initialized = true;
				}
				else
				{
					Error(_log, "Unsupported %ix%ix%i mode for '%s' device.", scr.xres, scr.yres, scr.bits_per_pixel, QSTRING_CSTR(foundDevice));
					close(_handle);
					_handle = -1;
				}
			}
			else
			{
				Error(_log, "Could not get the framebuffer dimension for '%s' device. Reason: %s (%i)", QSTRING_CSTR(foundDevice), std::strerror(errno), errno);
				close(_handle);
				_handle = -1;
			}
		}
	}

	return _initialized;
}


void FrameBufGrabber::getDevices()
{
	enumerateDevices(false);
}

bool FrameBufGrabber::isActivated()
{
	return !_deviceProperties.isEmpty();
}

void FrameBufGrabber::enumerateDevices(bool silent)
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
				Info(_log, "Found FrameBuffer device: %s", QSTRING_CSTR(path));
		}
	}
}

bool FrameBufGrabber::start()
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
		Error(_log, "Start failed (%s)", e.what());
	}

	return false;
}

void FrameBufGrabber::stop()
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

		_semaphore.release();
		Info(_log, "Stopped");
	}
}

void FrameBufGrabber::grabFrame()
{
	bool stopNow = false;
	bool checkAML = false;

	// Aseguramos que solo haya un hilo ejecutando la captura
	if (_semaphore.tryAcquire()) {
		try {
			if (_initialized) {
				// Verificar si hay video en Amlogic
				bool isVideoPlaying = isVideoPlayingAML();

				// Cambiar de dispositivo si es necesario
				if (isVideoPlaying != _usingAmlogic) {
					if (isVideoPlaying) {
						if (!_usingAmlogic)	Info(_log, "Change to AML");
						// Cambiar a Amlogic
						//uninit(); // Detener el framebuffer
						_usingAmlogic = initAmlogic(); // Inicializar amvideocap0
					}
					else {
						Info(_log, "Change to FB");
						// Cambiar a framebuffer
						_usingAmlogic = !stopAmlogic(); // Detener amvideocap0. Si tiene exito, devuelve true, asi que lo negamos.
						//start(); // Reiniciar el framebuffer
					}
				}

				// Capturar el frame según el dispositivo actual
				if (_usingAmlogic) {
					if (!checkAML)	Info(_log, "Grabbing AML. checkAML = %d", checkAML);
					checkAML = grabFrameAmlogic();
				}
				else {
					Info(_log, "Grabbing FB");
					stopNow = grabFrameFramebuffer();
					if (stopNow) {
						uninit();
					}
				}
			}
		}
		catch (const std::exception& e) {
			Error(_log, "Error al capturar el frame: %s", e.what());
		}

		// Liberamos el semáforo para permitir la captura en el siguiente ciclo
		_semaphore.release();

	}
}

bool FrameBufGrabber::grabFrameFramebuffer()
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
		_handle = open(QSTRING_CSTR(_actualDeviceName), O_RDONLY);

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
	}
	else
	{
		Error(_log, "Could not read the framebuffer dimension.");
		return true;
	}
}


void FrameBufGrabber::setCropping(unsigned cropLeft, unsigned cropRight, unsigned cropTop, unsigned cropBottom)
{
	_cropLeft = cropLeft;
	_cropRight = cropRight;
	_cropTop = cropTop;
	_cropBottom = cropBottom;
}


bool FrameBufGrabber::grabFrameAmlogic()
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

		int linelen = ((_width + 31) & ~31) * 3;
		size_t _bytesToRead = linelen * _height;

		if (_width * _height == 0) {
			_bytesToRead = 1920 * 1088 * 3;
			base = malloc(1920 * 1088 * 3);
			_width = _height = 0;
		}
		else {
			base = malloc(_bytesToRead);
			Info(_log, "Calculated linelen: %d", linelen);
			Info(_log, "Calculated _bytesToRead: %zu", _bytesToRead);
		}
		if (!base) {
			printf("Malloc _bytesToRead %zu failed\n", _bytesToRead);
			return false;
		}


		// Read the snapshot into the memory
		//ssize_t bytesRead = pread(_captureDev, _image_ptr, _bytesToRead, 0);

		Info(_log, "Successfully set capture frame size to: %dx%d", _width, _height);


		ssize_t bytesRead = pread(_captureDev, base, _bytesToRead, 0);

		if (bytesRead < 0 && !EAGAIN && errno > 0)
		{
			Error(_log, "Capture frame failed  failed - Retrying. Error [%d] - %s", errno, strerror(errno));
			return false;
		}
		else
		{
			if (bytesRead != -1 && static_cast<ssize_t>(_bytesToRead) != bytesRead)
			{
				// Read of snapshot failed
				Error(_log, "Capture failed to grab entire image [bytesToRead(%d) != bytesRead(%d)]", _bytesToRead, bytesRead);
				return false;
			}
			else {
				//If bytesRead = -1 but no error or EAGAIN or ENODATA, return last image to cover video pausing scenario
				// EAGAIN : // 11 - Resource temporarily unavailable
				// ENODATA: // 61 - No data available
				//uint8_t* memHandle = static_cast<uint8_t*>(mmap(nullptr, format.smem_len, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, _handle, 0));
				//processSystemFrameBGR(memHandle, linelen);
				//processSystemFrameBGR(_image_ptr, linelen);


				currentTime = std::time(nullptr);
				if (!((currentTime - lastCaptureTime < 5) || (savedImages.size() >= MAX_IMAGES)))
				{					
					lastCaptureTime = currentTime;

					std::tm tm = *std::localtime(&currentTime);
					std::ostringstream filename;
					//filename << "capture_" << std::put_time(&tm, "%y%m%d%H%M%S") << ".rgb";
					filename << "/storage/.kodi/temp/capture_" << std::put_time(&tm, "%y%m%d%H%M%S") << ".rgb";

					/*if (savedImages.size() >= MAX_IMAGES)
					{
						std::filesystem::remove(savedImages.front());
						savedImages.erase(savedImages.begin());
					}*/

					std::ofstream outFile(filename.str(), std::ios::binary);
					if (outFile.is_open())
					{
						outFile.write(reinterpret_cast<char*>(base), bytesRead);
						outFile.close();
						savedImages.push_back(filename.str());
						Info(_log, "Saved captured frame to %s", filename.str().c_str());
					}
					else
					{
						Error(_log, "Failed to open file %s for writing", filename.str().c_str());
					}

				}
				processSystemFrameBGR(static_cast<uint8_t*>(base), bufsize);
				return true;
			}
		}
	}
	return true;
}

bool FrameBufGrabber::initAmlogic()
{
	Info(_log, "Starting Amlogic capture device...");	
	try {
		
		_captureDev = open(DEFAULT_CAPTURE_DEVICE, O_RDWR);
		if (_captureDev < 0) {
			Error(_log, "Failed to open Amlogic capture device: %s", strerror(errno));
			return false;
		}

		Info(_log, "Amlogic capture device opened.");
		return true;
		
	}
	catch (const std::exception& e) {
		Error(_log, "Failed to open Amlogic capture device: %s", e.what());
		return false;
	}
}

bool FrameBufGrabber::stopAmlogic()
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
		Error(_log, "Failed to stop Amlogic capture device: %s", e.what());
		return false;
	}
}

void FrameBufGrabber::closeDeviceAML(int& fd)
{
	if (fd >= 0)
	{
		::close(fd);
		fd = -1;
	}
}

bool FrameBufGrabber::openDeviceAML(int& fd, const char* dev)
{
	bool rc = true;
	if (fd < 0)
	{
		fd = ::open(dev, O_RDWR);
		if (fd < 0)
		{
			rc = false;
		}
	}
	return rc;
}

bool FrameBufGrabber::isVideoPlayingAML()
{
	bool rc = false;
	if (QFile::exists(DEFAULT_VIDEO_DEVICE))
	{
		int videoDisabled = 1;
		if (!openDeviceAML(_videoDev, DEFAULT_VIDEO_DEVICE))
		{
			Error(_log, "Failed to open video device(%s): %d - %s", DEFAULT_VIDEO_DEVICE, errno, strerror(errno));
		}
		else
		{
			// Check the video disabled flag
			if (ioctl(_videoDev, AMSTREAM_IOC_GET_VIDEO_DISABLE, &videoDisabled) < 0)
			{
				Error(_log, "Failed to retrieve video state from device: %d - %s", errno, strerror(errno));
				closeDeviceAML(_videoDev);
			}
			else
			{
				if (videoDisabled == 0)
				{
					rc = true;
				}
			}
		}

	}
	return rc;
}
