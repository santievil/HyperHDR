#pragma once

// stl includes
#include <vector>
#include <map>
#include <chrono>

// Qt includes
#include <QObject>
#include <QSocketNotifier>
#include <QRectF>
#include <QMap>
#include <QMultiMap>
#include <QTimer>
#include <QSemaphore>

// util includes
#include <utils/PixelFormat.h>
#include <base/Grabber.h>
#include <utils/Components.h>

//AML
#include "../sources/grabber/linux/amlogic/Amvideocap.h"
#include <utils/ImageAML.h>
#include <utils/ColorBgrAML.h>

class FrameBufGrabber : public Grabber
{
	Q_OBJECT

public:

	FrameBufGrabber(const QString& device, const QString& configurationPath);

	~FrameBufGrabber();

	void setHdrToneMappingEnabled(int mode) override;

	void setCropping(unsigned cropLeft, unsigned cropRight, unsigned cropTop, unsigned cropBottom) override;

	bool isActivated();

	void stateChanged(bool state);

private slots:

	void grabFrame();

public slots:

	bool start() override;

	void stop() override;

	void newWorkerFrameHandler(unsigned int workerIndex, Image<ColorRgb> image, quint64 sourceCount, qint64 _frameBegin) override {};

	void newWorkerFrameErrorHandler(unsigned int workerIndex, QString error, quint64 sourceCount) override {};

private:
	QString GetSharedLut();

	void enumerateDevices(bool silent);

	void loadLutFile(PixelFormat color = PixelFormat::NO_CHANGE);
	
	void getDevices();

	bool init() override;

	void uninit() override;

	//AMLOGIC
	bool isVideoPlayingAML();
	void closeDeviceAML(int& fd);
	bool openDeviceAML(int& fd, const char* dev);
	int  _captureDev=-1;
	int  _videoDev=-1;
	
	ImageAML<ColorBgr> _image_bgr;
	void* _image_ptr;
	void* base;
	//AMLssize_t      _bytesToRead;
	ssize_t      _bytesToRead;

	int             _lastError;
	bool            _videoPlaying;
	bool _usingAmlogic = false; // Indica si estamos usando Amlogic
	bool initAmlogic();
	void stopAmlogic();
	void grabFrameAmlogic();
	bool grabFrameFramebuffer();
		
private:
	QString		_configurationPath;
	QTimer		_timer;
	QSemaphore	_semaphore;
	int			_handle;
};
