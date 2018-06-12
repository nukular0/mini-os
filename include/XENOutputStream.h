/*
 * XENOutputStream.h
 *
 *  Created on: 22.11.2016
 *      Author: hendrik
 */

#ifndef INCLUDE_XENOUTPUTSTREAM_H_
#define INCLUDE_XENOUTPUTSTREAM_H_

#include <OutputStream.h>

class XENOutputStream : public OutputStream {
public:
	XENOutputStream();

	static XENOutputStream mInstance;
	void writeToDevice();
protected:

private:

};

#endif /* INCLUDE_XENOUTPUTSTREAM_H_ */
