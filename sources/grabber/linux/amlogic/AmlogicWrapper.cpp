
#include <QMetaType>
#include <grabber/linux/amlogic/AmlogicWrapper.h>


AmlogicWrapper::AmlogicWrapper(const QString& device,
	const QString& configurationPath)
	: SystemWrapper("AMLOGIC_SYSTEM:" + device.left(14), &_grabber)
	, _grabber(device, configurationPath)
{
	qRegisterMetaType<Image<ColorRgb>>("Image<ColorRgb>");
	connect(&_grabber, &Grabber::SignalNewCapturedFrame, this, &SystemWrapper::newCapturedFrameHandler, Qt::DirectConnection);
	connect(&_grabber, &Grabber::SignalCapturingException, this, &SystemWrapper::capturingExceptionHandler, Qt::DirectConnection);
}

QString AmlogicWrapper::getGrabberInfo()
{
	return "amlogic";
}

bool AmlogicWrapper::isActivated(bool forced)
{
	return _grabber.isActivated();
}


/*#include <grabber/linux/amlogic/AmlogicWrapper.h>

AmlogicWrapper::AmlogicWrapper(int updateRate_Hz, int pixelDecimation)
	: GrabberWrapper(GRABBERTYPE, &_grabber, updateRate_Hz)
	, _grabber()
{
	_grabber.setPixelDecimation(pixelDecimation);
}

AmlogicWrapper::AmlogicWrapper(const QJsonDocument& grabberConfig)
	: AmlogicWrapper(GrabberWrapper::DEFAULT_RATE_HZ,
					 GrabberWrapper::DEFAULT_PIXELDECIMATION)
{
	//this->handleSettingsUpdate(settings::SYSTEMCAPTURE, grabberConfig);
	this->handleSettingsUpdate(settings::type::SYSTEMGRABBER, grabberConfig);
	
}

void AmlogicWrapper::action()
{
	transferFrame(_grabber);
}

bool AmlogicWrapper::isActivated(bool forced)
{
	//return _grabber.isActivated();
	return true;
}
*/
