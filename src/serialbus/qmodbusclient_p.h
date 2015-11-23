/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtSerialBus module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QMODBUSCLIENT_P_H
#define QMODBUSCLIENT_P_H

#include <QtSerialBus/qmodbusclient.h>
#include <QtSerialBus/private/qmodbusdevice_p.h>
#include <QtSerialBus/qmodbuspdu.h>

#include <QtCore/qtimer.h>
#include <QtCore/qdebug.h>

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

QT_BEGIN_NAMESPACE

class Q_AUTOTEST_EXPORT QModbusClientPrivate : public QModbusDevicePrivate
{
    Q_DECLARE_PUBLIC(QModbusClient)

public:
    QModbusClientPrivate() Q_DECL_EQ_DEFAULT;
    virtual ~QModbusClientPrivate() Q_DECL_EQ_DEFAULT;

    QModbusReply *sendRequest(const QModbusRequest &request, int serverAddress,
                              const QModbusDataUnit *const unit);
    QModbusRequest createReadRequest(const QModbusDataUnit &data) const;
    QModbusRequest createWriteRequest(const QModbusDataUnit &data) const;
    QModbusRequest createRWRequest(const QModbusDataUnit &read, const QModbusDataUnit &write) const;

    bool processResponse(const QModbusResponse &response, QModbusDataUnit *data);
    bool processReadCoilsResponse(const QModbusResponse &response, QModbusDataUnit *data);
    bool processReadDiscreteInputsResponse(const QModbusResponse &response, QModbusDataUnit *data);
    bool processReadHoldingRegistersResponse(const QModbusResponse &response,
                                             QModbusDataUnit *data);
    bool processReadInputRegistersResponse(const QModbusResponse &response, QModbusDataUnit *data);
    bool processWriteSingleCoilResponse(const QModbusResponse &response, QModbusDataUnit *data);
    bool processWriteSingleRegisterResponse(const QModbusResponse &response,
                                            QModbusDataUnit *data);
    bool processWriteMultipleCoilsResponse(const QModbusResponse &response, QModbusDataUnit *data);
    bool processWriteMultipleRegistersResponse(const QModbusResponse &response,
                                               QModbusDataUnit *data);
    bool processReadWriteMultipleRegistersResponse(const QModbusResponse &response,
                                                  QModbusDataUnit *data);

    void startResponseTimer();
    void stopResponseTimer();

    virtual void handleResponseTimeout() {}
    virtual QModbusReply *enqueueRequest(const QModbusRequest &, int, const QModbusDataUnit &,
                                         QModbusReply::ReplyType) {
        return Q_NULLPTR;
    }
    // TODO: Review once we have a transport layer in place.
    virtual bool isOpen() const { return false; }

    int m_responseTimeoutDuration = 200;
    QTimer *m_responseTimer = Q_NULLPTR;

    struct QueueElement {
        QPointer<QModbusReply> reply;
        QModbusRequest requestPdu;
        QModbusDataUnit unit;
    };
    void processQueueElement(const QModbusResponse &pdu, const QueueElement &element);
};

QT_END_NAMESPACE

#endif // QMODBUSCLIENT_P_H

