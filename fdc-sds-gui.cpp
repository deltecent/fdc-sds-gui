/**********************************************************************************
*
*  Altair FDC+ Serial Disk Server
*      This program serves Altair disk images over a high speed serial port
*      for computers running the FDC+ Enhanced Floppy Disk Controller.
*
*     Version     Date        Author         Notes
*      1.0     10/11/2020     P. Linstruth   Original
*
***********************************************************************************
*
* This version is based on the FDC+ Serial Drive Server 1.3 by Mike Douglas
*
***********************************************************************************
*
*  Communication with the server is over a serial port at 403.2K Baud, 8N1.
*  All transactions are initiated by the FDC. The second choice for baud rate
*  is 460.8K. Finally, 230.4K is the most likely supported baud rate on the PC
*  if 403.2K and 460.8K aren't avaialable.
*
*  403.2K is the preferred rate as it allows full-speed operation and is the
*  most accurate of the three baud rate choices on the FDC. 460.8K also allows
*  full speed operation, but the baud rate is off by about 3.5%. This works, but
*  is borderline. 230.4K is available on most all serial ports, is within 2% of
*  the FDC baud rate, but runs at 80%-90% of real disk speed.
*
*  FDC TO SERVER COMMANDS
*    Commands from the FDC to the server are fixed length, ten byte messages. The 
*    first four bytes are a command in ASCII, the remaining six bytes are grouped
*    as three 16 bit words (little endian). The checksum is the 16 bit sum of the
*    first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command       Parameter 1         Parameter 2           Checksum
*
*    Commands:
*      STAT - Provide and request drive status. The FDC sends the selected drive
*             number and head load status in Parameter 1 and the current track 
*             number in Parameter 2. The Server responds with drive mount status
*             (see below). The LSB of Parameter 1 contains the currently selected
*             drive number or 0xff is no drive is selected. The MSB of parameter 1
;             is non-zero if the head is loaded, zero if not loaded.
*
*             The FDC issues the STAT command about ten times per second so that
*             head status and track number information is updated quickly. The 
*             server may also want to assume the drive is selected, the head is
*             loaded, and update the track number whenever a READ is received.
*
*      READ - Read specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             length is in Parameter 2 and must be the track length. Also see
*             "Transfer of Track Data" below.
*
*      WRIT - Write specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             must be track length. Server responds with WRIT response when ready
*             for the FDC to send the track of data. See "Transfer of Track Data" below.
*
*
*  SERVER TO FDC 
*    Reponses from the server to the FDC are fixed length, ten byte messages. The 
*    first four bytes are a response command in ASCII, the remaining six bytes are
*    grouped as three 16 bit words (little endian). The checksum is the 16 bit sum
*    of the first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command      Response Code        Reponse Data          Checksum
*
*    Commands:
*      STAT - Returns drive status in Response Data with one bit per drive. "1" means a
*             drive image is mounted, "0" means not mounted. Bits 15-0 correspond to
*             drive numbers 15-0. Response code is ignored by the FDC.
*
*      WRIT - Issued in repsonse to a WRIT command from the FDC. This response is
*             used to tell the FDC that the server is ready to accept continuous transfer
*             of a full track of data (response code word set to "OK." If the request
*             can't be fulfilled (e.g., specified drive not mounted), the reponse code
*             is set to NOT READY. The Response Data word is don't care.
*
*      WSTA - Final status of the write command after receiving the track data is returned
*             in the repsonse code field. The Response Data word is don't care.
*
*    Reponse Code:
*      0x0000 - OK
*      0x0001 - Not Ready (e.g., write request to unmounted drive)
*      0x0002 - Checksum error (e.g., on the block of write data)
*      0x0003 - Write error (e.g., write to disk failed)
*
*
*  TRANSFER OF TRACK DATA
*    Track data is sent as a sequence of bytes followed by a 16 bit, little endian 
*    checksum. Note the Transfer Length field does NOT include the two bytes of
*    the checksum. The following notes apply to both the FDC and the server.
*
*  ERROR RECOVERY
*    The FDC uses a timeout of one second after the last byte of a message or data block
*        is sent to determine if a transmission was ignored.
*
*    The server should ignore commands with an invalid checksum. The FDC may retry the
*        command if no response is received. An invalid checksum on a block of write
*        data should not be ignored, instead, the WRIT response should have the
*        Reponse Code field set to 0x002, checksum error.
*
*    The FDC ignores responses with an invalid checksum. The FDC may retry the command
*        that generated the response by sending the command again.
*
***********************************************************************************/

#include <QtWidgets>
#include <QMessageBox>

#include "fdc-sds-gui.h"
#include "grnled.xpm"
#include "redled.xpm"

FDCDialog::FDCDialog(QWidget *parent)
	: QDialog(parent)
{
	int drive;

	// Title
	setWindowTitle(tr("FDC+ Serial Drive Server"));

	// Pixmaps
	grnLED = new QPixmap(greenled_xpm);
	redLED = new QPixmap(redled_xpm);

	// Layouts
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *commLayout = new QHBoxLayout;
	QGroupBox *driveGroup[MAX_DRIVE];
	QVBoxLayout *driveLayout[MAX_DRIVE];
	QHBoxLayout *row1Layout[MAX_DRIVE];
	QHBoxLayout *row2Layout[MAX_DRIVE];
	QHBoxLayout *row3Layout[MAX_DRIVE];
	QHBoxLayout *enabledLayout[MAX_DRIVE];
	QHBoxLayout *headloadLayout[MAX_DRIVE];
	QHBoxLayout *infoLayout = new QHBoxLayout;

	// Setup drive widgets and status
	for (drive = 0; drive < MAX_DRIVE; drive++) {
		row1Layout[drive] = new QHBoxLayout;
		row2Layout[drive] = new QHBoxLayout;
		row3Layout[drive] = new QHBoxLayout;

		label = new QLabel(QString("Drive %1").arg(drive));
		row1Layout[drive]->addWidget(label);
		label = new QLabel(tr("Track"));
		row1Layout[drive]->addWidget(label);

		trackProgress[drive] = new QProgressBar;
		trackProgress[drive]->setMinimum(0);
		trackProgress[drive]->setFormat(QString("%v"));
		trackProgress[drive]->setAlignment(Qt::AlignLeft);  
		trackProgress[drive]->setTextVisible(false);
		row1Layout[drive]->addWidget(trackProgress[drive]);

		fileName[drive] = new QLineEdit;
		fileName[drive]->setReadOnly(true);
		fileName[drive]->setEnabled(false);
		row2Layout[drive]->addWidget(fileName[drive]);

		loadButton[drive] = new QPushButton(tr("Load"), this);
		unloadButton[drive] = new QPushButton(tr("Unload"), this);
		unloadButton[drive]->setEnabled(false);

		enabledLayout[drive] = new QHBoxLayout;
		label = new QLabel(tr("Enabled"));
		enabledLabel[drive] = new QLabel;
		enabledLabel[drive]->setPixmap(*redLED);
		enabledLayout[drive]->addWidget(label);
		enabledLayout[drive]->addWidget(enabledLabel[drive]);

		headloadLayout[drive] = new QHBoxLayout;
		label = new QLabel(tr("Head Load"));
		headloadLabel[drive] = new QLabel;
		headloadLabel[drive]->setPixmap(*redLED);
		headloadLayout[drive]->addWidget(label);
		headloadLayout[drive]->addWidget(headloadLabel[drive]);

		row3Layout[drive]->addWidget(loadButton[drive]);
		row3Layout[drive]->addWidget(unloadButton[drive]);
		row3Layout[drive]->addLayout(enabledLayout[drive]);
		row3Layout[drive]->addLayout(headloadLayout[drive]);

		connect(loadButton[drive], &QPushButton::clicked, [this, drive] { loadButtonSlot(drive); });
		connect(unloadButton[drive], &QPushButton::clicked, [this, drive] { unloadButtonSlot(drive); });

		driveLayout[drive] = new QVBoxLayout;
		driveLayout[drive]->addLayout(row1Layout[drive]);
		driveLayout[drive]->addLayout(row2Layout[drive]);
		driveLayout[drive]->addLayout(row3Layout[drive]);

		driveGroup[drive] = new QGroupBox;
		driveGroup[drive]->setLayout(driveLayout[drive]);

		curTrack[drive] = 0;
		enableStatus[drive] = false;
		headStatus[drive] = false;

		openMode[drive] = QIODevice::ReadWrite;
		driveFile[drive] = new QFile;
	}

	// Information
	label = new QLabel(tr("FDC+ Serial Drive Server v1.0"));
	infoLayout->addWidget(label);
	label = new QLabel(tr("(c)2020 Deltec Enterprises"));
	infoLayout->addWidget(label);

	// Communications Ports
	serialPortBox = new QComboBox;
	serialPorts = QSerialPortInfo::availablePorts();
	for (const QSerialPortInfo &info : serialPorts) {
		serialPortBox->addItem(info.portName());
	}
	serialPortBox->setPlaceholderText(tr("None"));
	serialPortBox->setCurrentIndex(-1);
	serialPortBox->setStyleSheet("color: white;");
	connect(serialPortBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ serialPortSlot(index); });

	commLayout->addWidget(serialPortBox);

	baudRateBox = new QComboBox;
	baudRateBox->addItem("230.4K", 230400);
	baudRateBox->addItem("403.2K", 403200);
	baudRateBox->addItem("460.8K", 460800);
	baudRateBox->setStyleSheet("color: white;");
	connect(baudRateBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ baudRateSlot(index); });

	commLayout->addWidget(baudRateBox);

	mainLayout->addLayout(commLayout);

	for (drive = 0; drive < MAX_DRIVE; drive++) {
		mainLayout->addWidget(driveGroup[drive]);
	}

	mainLayout->addLayout(infoLayout);

	setLayout(mainLayout);

	// Serial Port Object
	serialPort = new QSerialPort;
	baudRate = baudRateBox->currentData().toInt();
	cmdBufIdx = 0;

	// Start timer
	timer = new QTimer(this);
	timer->setInterval(10);		// 10ms
	connect(timer, &QTimer::timeout, this, &FDCDialog::timerSlot);
	timer->start();

	debugWindow = new QTextEdit;
	this->layout()->addWidget(debugWindow);
}

void FDCDialog::serialPortSlot(int index)
{
	serialPort->setPortName(serialPortBox->itemText(index));
	QMessageBox::information(this, "Serial Port", QString("Setting serial port to '%1'").arg(serialPort->portName()));

	updateSerialPort();
}

void FDCDialog::baudRateSlot(int index)
{
	baudRate = baudRateBox->itemData(index).toInt();
	QMessageBox::information(this, "Baud Rate", QString("Setting baud rate to %1").arg(baudRate));

	updateSerialPort();
}

void FDCDialog::loadButtonSlot(int drive)
{
	QString fname = QFileDialog::getOpenFileName(this, tr("Open Disk Image"), savePath, tr("Disk Image Files (*.dsk);;All Files (*.*)"));

	if (fname.length()) {
		driveFile[drive]->setFileName(fname);

		if (driveFile[drive]->open(openMode[drive])) {
			QFileInfo finfo(fname);

			savePath = finfo.filePath();

			fileName[drive]->setText(finfo.fileName());
			fileName[drive]->setEnabled(true);

      			qint64 filesize = driveFile[drive]->size();
			if (filesize < 200000) {
				maxTrack[drive] = 34;
			}
			else if (filesize < 500000) {
				maxTrack[drive] = 76;
			}
			else {
				maxTrack[drive] = 2047;
			}
			trackProgress[drive]->setMaximum(maxTrack[drive]);
			trackProgress[drive]->setValue(maxTrack[drive]);
			trackProgress[drive]->setTextVisible(true);

			loadButton[drive]->setEnabled(false);
			unloadButton[drive]->setEnabled(true);
			unloadButton[drive]->setFocus(Qt::OtherFocusReason);
		}
		else {
			QMessageBox::critical(this, "Mount Error", QString("Could not open disk mage '%1'").arg(fname));
		}
	}
}

void FDCDialog::unloadButtonSlot(int drive)
{
	if (driveFile[drive]->isOpen()) {
		driveFile[drive]->close();

		fileName[drive]->setText(QString(""));
		fileName[drive]->setEnabled(false);

		trackProgress[drive]->setTextVisible(false);

		maxTrack[drive] = 0;
		curTrack[drive] = 0;
		enableStatus[drive] = false;
		headStatus[drive] = false;

		updateIndicators(drive);

		loadButton[drive]->setEnabled(true);
		loadButton[drive]->setFocus(Qt::OtherFocusReason);
		unloadButton[drive]->setEnabled(false);
	}
}

void FDCDialog::timerSlot()
{
	int i;
	quint16 driveNum;
	quint16 trackLen;
	qint64 bytesRead;
	qint64 bytesAvail;
	quint16 checksum;

	if (!serialPort->isOpen()) {
		return;
	}

	bytesRead = serialPort->read((char *) &cmdBuf.asBytes[cmdBufIdx], CMDBUF_SIZE-cmdBufIdx);

	if (bytesRead == 0) {
		cmdBufIdx = 0;
		return;
	}

	cmdBufIdx += bytesRead;

	debugWindow->append(QString("Received %1 bytes. Index %2.").arg(bytesRead).arg(cmdBufIdx));

	if (cmdBufIdx < CMDBUF_SIZE) {
		cmdBuf.asBytes[cmdBufIdx] = 0;
		return;
	}

	cmdBufIdx = 0;

	// Calculate and validate checksum
//	checksum = 0;

//	for (i = 0; i < COMMAND_LENGTH; i++) {
//		checksum += cmdBuf.asBytes[i];
//		debugWindow->append(QString("byte=%1 chksum=%2").arg((ushort)cmdBuf.asBytes[i]).arg(checksum,4,16));
//	}

	checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);

	if (checksum != cmdBuf.checksum) {
		debugWindow->append(QString("calc=%1 recv=%2").arg(checksum,4,16).arg(cmdBuf.checksum,4,16));
	}

	// READ command
	if (QString(cmdBuf.command).left(4) == QString("READ")) {
		debugWindow->append(QString("READ command"));

		driveNum = cmdBuf.param1 >> 12;

		// If drive not mounted, ignore
		if (!driveFile[driveNum]->isOpen()) {
			debugWindow->append(QString("READ failed - drive %1 not mounted").arg(driveNum));
			return;
		}

		// Track in lower 12 bits
		curTrack[driveNum] = cmdBuf.param1 & 0x0fff;
		trackLen = cmdBuf.param2;

		// If the requested track length is too long, ignore
		if (trackLen > TRACKBUF_LEN) {
			debugWindow->append(QString("READ failed - request track len %1 > %2 bytes").arg(trackLen).arg(TRACKBUF_LEN));
			return;
		}

		updateIndicators(driveNum);

		driveFile[driveNum]->seek(curTrack[driveNum] * trackLen);

		if ((bytesRead = driveFile[driveNum]->read((char *) trackBuf, trackLen)) != trackLen) {
			debugWindow->append(QString("READ failed - read %1 of %2 bytes").arg(bytesRead).arg(trackLen));
			return;	// Ignore reads past end of file
		}

//		checksum = 0;
//		for (i = 0; i < trackLen; i++) {
//			checksum += trackBuf[i];
//		}
		checksum = calcChecksum(trackBuf, trackLen);
		trackBuf[trackLen] = checksum & 0x00ff;			// LSB of checksum
		trackBuf[trackLen+1] = (checksum >> 8) & 0x00ff;	// MSB of checksum

		serialPort->write((char *) trackBuf, trackLen + 2);

		debugWindow->append(QString("READ response %1 bytes + crc").arg(trackLen));
	}

	// WRIT command
	else if (QString(cmdBuf.command).left(4) == QString("WRIT")) {
		debugWindow->append(QString("WRIT command"));

		driveNum = cmdBuf.param1 >> 12;
		curTrack[driveNum] = cmdBuf.param1 & 0x0fff;
		trackLen = cmdBuf.param2;

		debugWindow->append(QString("WRIT driveNum=%1 track=%2 tracklen=%3").arg(driveNum).arg(curTrack[driveNum]).arg(trackLen));

		// If drive not mounted, ignore
		if (!driveFile[driveNum]->isOpen()) {
			cmdBuf.rcode = STAT_NOT_READY;
			debugWindow->append(QString("WRITE failed - drive %1 not mounted").arg(driveNum));
		}
		else {
			cmdBuf.rcode = STAT_OK;
		}

		// If the requested track length is too long, ignore
		if (trackLen > TRACKBUF_LEN) {
			debugWindow->append(QString("WRIT failed - request track len %1 > %2 bytes").arg(trackLen).arg(TRACKBUF_LEN));
			cmdBuf.rcode = STAT_NOT_READY;
		}
		else {
			debugWindow->append(QString("WRIT seeking to %1").arg(curTrack[driveNum] * trackLen));
			driveFile[driveNum]->seek(curTrack[driveNum] * trackLen);
		}

		// Send WRIT response
//		cmdBuf.checksum = 0;
//		for (i = 0; i < COMMAND_LENGTH; i++) {
//			cmdBuf.checksum += cmdBuf.asBytes[i];
//		}
		cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);

		if (cmdBuf.rcode == STAT_OK) {
			debugWindow->append(QString("WRIT command is OK"));
			serialPort->write((char *) cmdBuf.asBytes, CMDBUF_SIZE);

			trkBufIdx = 0;

			do {
				bytesAvail = serialPort->waitForReadyRead(100);
				trkBufIdx += serialPort->read((char *) &trackBuf[trkBufIdx], TRACKBUF_LEN_CRC-trkBufIdx);
			} while (trkBufIdx < trackLen + 2 && bytesAvail);

			debugWindow->append(QString("WRIT received %1 byte track").arg(trkBufIdx));

//			checksum = 0;
//			for (i = 0; i < trackLen; i++) {
//				checksum += trackBuf[i];
//			}
			checksum = calcChecksum(trackBuf, trackLen);

			if ((trkBufIdx = trackLen+2)
				&& ((checksum & 0xff) == trackBuf[trackLen])
				&& (((checksum >> 8) & 0xff)) == trackBuf[trackLen + 1]) {

				if (driveFile[driveNum]->write((char *) trackBuf, trackLen) != trackLen) {
					debugWindow->append(QString("WRIT write error"));
					cmdBuf.rcode = STAT_WRITE_ERR;
				}
			}
			else {
				debugWindow->append(QString("WRIT checksum error"));
				cmdBuf.rcode = STAT_CHECKSUM_ERR;
			}

			// Set response to WSTA
			cmdBuf.command[0] = 'W';
			cmdBuf.command[1] = 'S';
			cmdBuf.command[2] = 'T';
			cmdBuf.command[3] = 'A';

//			cmdBuf.checksum = 0;
//			for (i = 0; i < COMMAND_LENGTH; i++) {
//				cmdBuf.checksum += cmdBuf.asBytes[i];
//			}
			cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);
		}

		serialPort->write((char *) cmdBuf.asBytes, CMDBUF_SIZE);
	}

	// STAT command
	else if (QString(cmdBuf.command).left(4) == QString("STAT")) {
		debugWindow->append(QString("STAT command"));

		driveNum = cmdBuf.param1 & 0x00ff;
		debugWindow->append(QString("driveNum=%1").arg(driveNum));
		if (driveNum < MAX_DRIVE) {
			headStatus[driveNum] = (cmdBuf.param1 & 0xff00) >> 8;
			curTrack[driveNum] = cmdBuf.param2;
			updateIndicators(driveNum);
		}

		// Respond with status of mounted drives
		cmdBuf.rcode = STAT_OK;
		cmdBuf.rdata = 0;
		for (i = 0; i < MAX_DRIVE; i++) {
			if (driveFile[i]->isOpen()) {
				cmdBuf.rdata |= (1 << i);
			}
		}

//		cmdBuf.checksum = 0;
//		for (i = 0; i < COMMAND_LENGTH; i++) {
//			cmdBuf.checksum += cmdBuf.asBytes[i];
//		}
		cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);

		serialPort->write((char *) cmdBuf.asBytes, CMDBUF_SIZE);

		debugWindow->append(QString("STAT response code=%1 data=%2").arg(cmdBuf.rcode,4,16).arg(cmdBuf.rdata,4,16));
	}

	timer->start();
}

void FDCDialog::updateSerialPort()
{
	if (serialPort->isOpen()) {
		serialPort->close();
	}

	if (serialPort->open(QIODevice::ReadWrite)) {
		if (serialPort->setBaudRate(baudRate) == false) {
			QMessageBox::critical(this,
				"Serial Port Error",
				QString("Could not set baudrate to %1").arg(baudRate));
		}
		serialPort->setDataBits(QSerialPort::Data8);
		serialPort->setParity(QSerialPort::NoParity);
		serialPort->setStopBits(QSerialPort::OneStop);
		serialPort->setFlowControl(QSerialPort::NoFlowControl);
		serialPort->setDataTerminalReady(true);
		serialPort->setRequestToSend(true);
	}
	else {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString("Could not open serial port '%1' (%2)").arg(serialPort->portName()).arg(serialPort->error()));
	}
}

void FDCDialog::updateIndicators(int drive)
{
	int i;

	if (drive < MAX_DRIVE) {
		for (i = 0; i < MAX_DRIVE; i++) {
			enabledLabel[i]->setPixmap(*redLED);
			headloadLabel[i]->setPixmap(*redLED);
		}

		enabledLabel[drive]->setPixmap(*grnLED);
//		enabledLabel[drive]->repaint();

		if (headStatus[drive]) {
			headloadLabel[drive]->setPixmap(*grnLED);
		}

		if (maxTrack[drive] >= curTrack[drive]) {
			trackProgress[drive]->setValue(curTrack[drive]);
		}
	}
}

quint16 FDCDialog::calcChecksum(const quint8 *data, int length)
{
	int i;
	quint16 checksum;

	checksum = 0;

	for (i = 0; i < length; i++) {
		checksum += data[i];
	}

	return checksum;
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setStyle(QStyleFactory::create("Fusion"));
	FDCDialog *dialog = new FDCDialog;
	dialog->show();
	return app.exec();
}


