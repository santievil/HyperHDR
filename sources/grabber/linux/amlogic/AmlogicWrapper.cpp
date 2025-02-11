#include <grabber/linux/amlogic/AmlogicWrapper.h>

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
