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

#include <grabber/linux/amlogic/AmlogicGrabber.h>

const int  DEFAULT_FB_DEVICE_IDX = 0;
const char DEFAULT_VIDEO_DEVICE[] = "/dev/amvideo";
const char DEFAULT_CAPTURE_DEVICE[] = "/dev/amvideocap0";
const int  AMVIDEOCAP_WAIT_MAX_MS = 40;
const int  AMVIDEOCAP_DEFAULT_RATE_HZ = 25;

AmlogicGrabber::AmlogicGrabber(const QString& device, const QString& configurationPath)
	: Grabber(configurationPath, "AMLOGIC_SYSTEM:" + device.left(14))
	, _configurationPath(configurationPath)
	, _semaphore(1)
	, _handle(-1)
{
	_timer.setTimerType(Qt::PreciseTimer);
	connect(&_timer, &QTimer::timeout, this, &AmlogicGrabber::grabFrame);

	getDevices();
}

QString AmlogicGrabber::GetSharedLut()
{
	return "";
}

void AmlogicGrabber::loadLutFile(PixelFormat color)
{
}

void AmlogicGrabber::setHdrToneMappingEnabled(int mode)
{
}

AmlogicGrabber::~AmlogicGrabber()
{
	uninit();
}

void AmlogicGrabber::uninit()
{
	// stop if the grabber was not stopped
	if (_initialized)
	{
		stop();
		Debug(_log, "Uninit grabber: %s", QSTRING_CSTR(_deviceName));
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
		Info(_log, "Starting Amlogic grabber. Selected: '%s' (%i) max width: %d (%d) @ %d fps", QSTRING_CSTR(foundDevice), _deviceProperties[foundDevice].valid.first().input, _width, _height, _fps);
		Info(_log, "*************************************************************************************************");

		_handle = open(QSTRING_CSTR(foundDevice), O_RDONLY);
		if (_handle < 0)
		{
			Error(_log, "Could not open the amlogic device: '%s'. Reason: %s (%i)", QSTRING_CSTR(foundDevice), std::strerror(errno), errno);
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
				Error(_log, "Could not get the amlogic dimension for '%s' device. Reason: %s (%i)", QSTRING_CSTR(foundDevice), std::strerror(errno), errno);
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
	int maxDevice = 0;

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
				Info(_log, "Found Framebuffer device: %s", QSTRING_CSTR(path));

			maxDevice = i;
		}
	}	

	//Alvaroti detecta amlogic
	QString pathC = QString(DEFAULT_CAPTURE_DEVICE);
	QString pathV = QString(DEFAULT_VIDEO_DEVICE);
	//if (QFileInfo(pathC).exists() && QFileInfo(pathV).exists())
	if (QFile::exists(DEFAULT_VIDEO_DEVICE) && QFile::exists(DEFAULT_CAPTURE_DEVICE))
	{
		DeviceProperties properties;
		DevicePropertiesItem dpi;

		dpi.input = maxDevice++;
		properties.valid.append(dpi);

		_deviceProperties.insert(pathC, properties);

		if (!silent)
			Info(_log, "Found Amlogic device: %s", QSTRING_CSTR(pathC));
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
		Error(_log, "start failed (%s)", e.what());
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

		_semaphore.release();
		Info(_log, "Stopped");
	}
}

void AmlogicGrabber::grabFrame()
{
	bool stopNow = false;

	if (_semaphore.tryAcquire())
	{
		if (_initialized)
		{
			/// GETFRAME
			struct fb_var_screeninfo scr;
			bool isStillActive = false;

			if (ioctl(_handle, FBIOGET_VSCREENINFO, &scr) == 0)
			{
				isStillActive = true;
			}
			else
			{
				Warning(_log, "The handle is lost. Trying to restart the driver.");

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
							Error(_log, "Could not map the amlogic memory.");
							stopNow = true;
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
						}
					}
					else
					{
						Error(_log, "Could not read the amlogic properties.");
						stopNow = true;
					}
				}
			}
			else
			{
				Error(_log, "Could not read the amlogic dimension.");
				stopNow = true;
			}
		}
		_semaphore.release();
	}

	if (stopNow)
	{
		uninit();
	}
}


void AmlogicGrabber::setCropping(unsigned cropLeft, unsigned cropRight, unsigned cropTop, unsigned cropBottom)
{
	_cropLeft = cropLeft;
	_cropRight = cropRight;
	_cropTop = cropTop;
	_cropBottom = cropBottom;
}




/*// STL includes
#include <algorithm>
#include <cassert>
#include <iostream>

// Linux includes
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

// qt
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSize>

// Local includes
#include <utils/Logger.h>
#include <grabber/linux/amlogic/AmlogicGrabber.h>
#include "Amvideocap.h"

// Constants
namespace {
const bool verbose = false;

const int  DEFAULT_FB_DEVICE_IDX = 0;
const char DEFAULT_VIDEO_DEVICE[] = "/dev/amvideo";
const char DEFAULT_CAPTURE_DEVICE[] = "/dev/amvideocap0";
const int  AMVIDEOCAP_WAIT_MAX_MS = 40;
const int  AMVIDEOCAP_DEFAULT_RATE_HZ = 25;

} //End of constants

AmlogicGrabber::AmlogicGrabber()
	: Grabber("GRABBER-AMLOGIC") // Minimum required width or height is 160
	  , _captureDev(-1)
	  , _videoDev(-1)
	  , _lastError(0)
	  , _fbGrabber(DEFAULT_FB_DEVICE_IDX)
	  , _grabbingModeNotification(0)
{
	_image_ptr = _image_bgr.memptr();
	_useImageResampler = true;
}

AmlogicGrabber::~AmlogicGrabber()
{
	closeDevice(_captureDev);
	closeDevice(_videoDev);
}

bool AmlogicGrabber::setupScreen()
{
	bool rc (false);

	QSize screenSize = _fbGrabber.getScreenSize();
	if ( !screenSize.isEmpty() )
	{
		if (setWidthHeight(screenSize.width(), screenSize.height()))
		{
			rc = _fbGrabber.setupScreen();
		}
	}
	return rc;
}

bool AmlogicGrabber::openDevice(int &fd, const char* dev)
{
	bool rc = true;
	if (fd<0)
	{
		fd = ::open(dev, O_RDWR);
		if ( fd < 0)
		{
			rc = false;
		}
	}
	return rc;
}

void AmlogicGrabber::closeDevice(int &fd)
{
	if (fd >= 0)
	{
		::close(fd);
		fd = -1;
	}
}

bool AmlogicGrabber::isVideoPlaying()
{
	bool rc = false;
	if(QFile::exists(DEFAULT_VIDEO_DEVICE))
	{
		int videoDisabled = 1;
		if (!openDevice(_videoDev, DEFAULT_VIDEO_DEVICE))
		{
			Error(_log, "Failed to open video device(%s): %d - %s", DEFAULT_VIDEO_DEVICE, errno, strerror(errno));
		}
		else
		{
			// Check the video disabled flag
			if(ioctl(_videoDev, AMSTREAM_IOC_GET_VIDEO_DISABLE, &videoDisabled) < 0)
			{
				Error(_log, "Failed to retrieve video state from device: %d - %s", errno, strerror(errno));
				closeDevice(_videoDev);
			}
			else
			{
				if ( videoDisabled == 0 )
				{
					rc = true;
				}
			}
		}

	}
	return rc;
}

int AmlogicGrabber::grabFrame(Image<ColorRgb> & image)
{
	int rc = 0;
	if (_isEnabled && !_isDeviceInError)
	{
		// Make sure video is playing, else there is nothing to grab
		if (isVideoPlaying())
		{
			if (_grabbingModeNotification!=1)
			{
				Info(_log, "Switch to VPU capture mode");
				_grabbingModeNotification = 1;
				_lastError = 0;
			}

			if (grabFrame_amvideocap(image) < 0) {
				closeDevice(_captureDev);
				rc = -1;
			}
		}
		else
		{
			if (_grabbingModeNotification!=2)
			{
				Info( _log, "Switch to Framebuffer capture mode");
				_grabbingModeNotification = 2;
				_lastError = 0;
			}
			rc = _fbGrabber.grabFrame(image);
		}
	}
	return rc;
}

int AmlogicGrabber::grabFrame_amvideocap(Image<ColorRgb> & image)
{
	int rc = 0;

	// If the device is not open, attempt to open it
	if (_captureDev < 0)
	{
		if (! openDevice(_captureDev, DEFAULT_CAPTURE_DEVICE))
		{
			ErrorIf( _lastError != 1, _log,"Failed to open the AMLOGIC device (%d - %s):", errno, strerror(errno));
			_lastError = 1;
			rc = -1;
			return rc;
		}
	}

	long r1 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH, _width);
	long r2 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT, _height);
	long r3 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS, CAP_FLAG_AT_END);
	long r4 = ioctl(_captureDev, AMVIDEOCAP_IOW_SET_WANTFRAME_WAIT_MAX_MS, AMVIDEOCAP_WAIT_MAX_MS);

	if (r1<0 || r2<0 || r3<0 || r4<0 || _height==0 || _width==0)
	{
		ErrorIf(_lastError != 2,_log,"Failed to configure capture device (%d - %s)", errno, strerror(errno));
		_lastError = 2;
		rc = -1;
	}
	else
	{
		int linelen = ((_width + 31) & ~31) * 3;
		size_t _bytesToRead = linelen * _height;

		// Read the snapshot into the memory
		ssize_t bytesRead   = pread(_captureDev, _image_ptr, _bytesToRead, 0);

		if ( bytesRead < 0 && !EAGAIN && errno > 0 )
		{
			ErrorIf(_lastError != 3, _log,"Capture frame failed  failed - Retrying. Error [%d] - %s", errno, strerror(errno));
			_lastError = 3;
			rc = -1;
		}
		else
		{
			if (bytesRead != -1 && static_cast<ssize_t>(_bytesToRead) != bytesRead)
			{
				// Read of snapshot failed
				ErrorIf(_lastError != 4, _log,"Capture failed to grab entire image [bytesToRead(%d) != bytesRead(%d)]", _bytesToRead, bytesRead);
				_lastError = 4;
				rc = -1;
			}
			else {
				//If bytesRead = -1 but no error or EAGAIN or ENODATA, return last image to cover video pausing scenario
				// EAGAIN : // 11 - Resource temporarily unavailable
				// ENODATA: // 61 - No data available
				_imageResampler.processImage(static_cast<uint8_t*>(_image_ptr),
											  _width,
											  _height,
											  linelen,
											  PixelFormat::BGR24, image);
				_lastError = 0;
				rc = 0;
			}
		}
	}
	return rc;
}

QJsonObject AmlogicGrabber::discover(const QJsonObject& params)
{
	DebugIf(verbose, _log, "params: [%s]", QString(QJsonDocument(params).toJson(QJsonDocument::Compact)).toUtf8().constData());

	QJsonObject inputsDiscovered;

	if(QFile::exists(DEFAULT_VIDEO_DEVICE) && QFile::exists(DEFAULT_CAPTURE_DEVICE) )
	{
		QJsonArray video_inputs;

		QSize screenSize = _fbGrabber.getScreenSize();
		if ( !screenSize.isEmpty() )
		{
			int fbIdx = _fbGrabber.getPath().right(1).toInt();

			DebugIf(verbose, _log, "FB device [%s] found with resolution: %dx%d", QSTRING_CSTR(_fbGrabber.getPath()), screenSize.width(), screenSize.height());
			QJsonArray fps = { 1, 5, 10, 15, 20, 25, 30};

			QJsonObject in;

			QString displayName;
			displayName = QString("Display%1").arg(fbIdx);

			in["name"] = displayName;
			in["inputIdx"] = fbIdx;

			QJsonArray formats;
			QJsonObject format;

			QJsonArray resolutionArray;

			QJsonObject resolution;

			resolution["width"] = screenSize.width();
			resolution["height"] = screenSize.height();
			resolution["fps"] = fps;

			resolutionArray.append(resolution);

			format["resolutions"] = resolutionArray;
			formats.append(format);

			in["formats"] = formats;
			video_inputs.append(in);
		}

		if (!video_inputs.isEmpty())
		{
			inputsDiscovered["device"] = "amlogic";
			inputsDiscovered["device_name"] = "AmLogic";
			inputsDiscovered["type"] = "screen";
			inputsDiscovered["video_inputs"] = video_inputs;

			QJsonObject defaults, video_inputs_default, resolution_default;
			resolution_default["fps"] = AMVIDEOCAP_DEFAULT_RATE_HZ;
			video_inputs_default["resolution"] = resolution_default;
			video_inputs_default["inputIdx"] = 0;
			defaults["video_input"] = video_inputs_default;
			inputsDiscovered["default"] = defaults;
		}
	}

	if (inputsDiscovered.isEmpty())
	{
		DebugIf(verbose, _log, "No displays found to capture from!");
	}

	DebugIf(verbose, _log, "device: [%s]", QString(QJsonDocument(inputsDiscovered).toJson(QJsonDocument::Compact)).toUtf8().constData());

	return inputsDiscovered;
}

void AmlogicGrabber::setVideoMode(VideoMode mode)
{
	Grabber::setVideoMode(mode);
	_fbGrabber.setVideoMode(mode);
}

bool AmlogicGrabber::setPixelDecimation(int pixelDecimation)
{
	return ( Grabber::setPixelDecimation( pixelDecimation) &&
			 _fbGrabber.setPixelDecimation( pixelDecimation));
}

void AmlogicGrabber::setCropping(int cropLeft, int cropRight, int cropTop, int cropBottom)
{
	Grabber::setCropping(cropLeft, cropRight, cropTop, cropBottom);
	_fbGrabber.setCropping(cropLeft, cropRight, cropTop, cropBottom);
}

bool AmlogicGrabber::setWidthHeight(int width, int height)
{
	bool rc (false);
	if ( Grabber::setWidthHeight(width, height) )
	{
		_image_bgr.resize(static_cast<unsigned>(width), static_cast<unsigned>(height));
		_width = width;
		_height = height;
		_bytesToRead = _image_bgr.size();
		_image_ptr = _image_bgr.memptr();
		rc = _fbGrabber.setWidthHeight(width, height);
	}
	return rc;
}

bool AmlogicGrabber::setFramerate(int fps)
{
	return (Grabber::setFramerate(fps) &&
			 _fbGrabber.setFramerate(fps));
}

bool AmlogicGrabber::isActive()
{
	//return !_deviceProperties.isEmpty();
	return true;

}
*/
