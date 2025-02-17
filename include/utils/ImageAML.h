#pragma once

#include <QSharedDataPointer>

#include <utils/ImageDataAML.h>

template <typename Pixel_T>
class ImageAML
{
public:
	typedef Pixel_T pixel_type;

	ImageAML() :
		ImageAML(1, 1, Pixel_T())
	{
	}

	ImageAML(int width, int height) :
		ImageAML(width, height, Pixel_T())
	{
	}

	///
	/// Constructor for an ImageAML with specified width and height
	///
	/// @param width The width of the ImageAML
	/// @param height The height of the ImageAML
	/// @param background The color of the ImageAML
	///
	ImageAML(int width, int height, const Pixel_T background) :
		_d_ptr(new ImageDataAML<Pixel_T>(width, height, background))
	{
	}

	///
	/// Copy constructor for an ImageAML
	/// @param other The ImageAML which will be copied
	///
	ImageAML(const ImageAML & other)
	{
		_d_ptr = other._d_ptr;
	}

	ImageAML& operator=(ImageAML rhs)
	{
		// Define assignment operator in terms of the copy constructor
		// More to read: https://stackoverflow.com/questions/255612/dynamically-allocating-an-array-of-objects?answertab=active#tab-top
		_d_ptr = rhs._d_ptr;
		return *this;
	}

	void swap(ImageAML& s)
	{
		std::swap(this->_d_ptr, s._d_ptr);
	}

	ImageAML(ImageAML&& src) noexcept
	{
		std::swap(this->_d_ptr, src._d_ptr);
	}

	ImageAML& operator=(ImageAML&& src) noexcept
	{
		src.swap(*this);
		return *this;
	}

	///
	/// Destructor
	///
	~ImageAML()
	{
	}

	///
	/// Returns the width of the ImageAML
	///
	/// @return The width of the ImageAML
	///
	inline int width() const
	{
		return _d_ptr->width();
	}

	///
	/// Returns the height of the ImageAML
	///
	/// @return The height of the ImageAML
	///
	inline int height() const
	{
		return _d_ptr->height();
	}

	uint8_t red(unsigned pixel) const
	{
		return _d_ptr->red(pixel);
	}

	uint8_t green(unsigned pixel) const
	{
		return _d_ptr->green(pixel);
	}

	///
	/// Returns a const reference to a specified pixel in the ImageAML
	///
	/// @param x The x index
	/// @param y The y index
	///
	/// @return const reference to specified pixel
	///
	uint8_t blue(int pixel) const
	{
		return _d_ptr->blue(pixel);
	}

	///
	/// Returns a reference to a specified pixel in the ImageAML
	///
	/// @param x The x index
	/// @param y The y index
	const Pixel_T& operator()(int x, int y) const
	{
		return _d_ptr->operator()(x, y);
	}

	///
	/// @return reference to specified pixel
	///
	Pixel_T& operator()(int x, int y)
	{
		return _d_ptr->operator()(x, y);
	}

	/// Resize the ImageAML
	/// @param width The width of the ImageAML
	/// @param height The height of the ImageAML
	void resize(int width, int height)
	{
		_d_ptr->resize(width, height);
	}

	///
	/// Returns a memory pointer to the first pixel in the ImageAML
	/// @return The memory pointer to the first pixel
	///
	Pixel_T* memptr()
	{
		return _d_ptr->memptr();
	}

	///
	/// Returns a const memory pointer to the first pixel in the ImageAML
	/// @return The const memory pointer to the first pixel
	///
	const Pixel_T* memptr() const
	{
		return _d_ptr->memptr();
	}

	///
	/// Convert ImageAML of any color order to a RGB ImageAML.
	///
	/// @param[out] ImageAML  The ImageAML that buffers the output
	///
	void toRgb(ImageAML<ColorRgb>& ImageAML) const
	{
		_d_ptr->toRgb(*ImageAML._d_ptr);
	}

	///
	/// Get size of buffer
	///
	ssize_t size() const
	{
		return _d_ptr->size();
	}

	///
	/// Clear the ImageAML
	///
	void clear()
	{
		_d_ptr->clear();
	}

private:
	template<class T>
	friend class ImageAML;

	///
	/// Translate x and y coordinate to index of the underlying vector
	///
	/// @param x The x index
	/// @param y The y index
	///
	/// @return The index into the underlying data-vector
	///
	inline int toIndex(int x, int y) const
	{
		return _d_ptr->toIndex(x, y);
	}

	QSharedDataPointer<ImageDataAML<Pixel_T>>  _d_ptr;
};

