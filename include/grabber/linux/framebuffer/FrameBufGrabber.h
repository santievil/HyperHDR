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

/*#define CAP_FLAG_AT_END			2
#define AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH      		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x02, int)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT     		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x03, int)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_WAIT_MAX_MS     	_IOW(AMVIDEOCAP_IOC_MAGIC, 0x05, unsigned long long)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS     		_IOW(AMVIDEOCAP_IOC_MAGIC, 0x06, int)

#define AMSTREAM_IOC_MAGIC 'S'
#define AMSTREAM_IOC_GET_VIDEO_DISABLE	_IOR((AMSTREAM_IOC_MAGIC), 0x48, int)
*/

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
	int             _captureDev=-1;
	int             _videoDev=-1;

	ImageAML<ColorBgr> _image_bgr;
	void* _image_ptr;
	//AMLssize_t      _bytesToRead;
	ssize_t      _bytesToRead;

	int             _lastError;
	bool            _videoPlaying;
		
private:
	QString		_configurationPath;
	QTimer		_timer;
	QSemaphore	_semaphore;
	int			_handle;
};
