/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtBluetooth module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qbluetoothserver.h"
#include "qbluetoothserver_p.h"
#include "qbluetoothsocket.h"

#include <QtCore/QSocketNotifier>

#include <QtCore/QDebug>

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/l2cap.h>

#include <errno.h>

QT_BEGIN_NAMESPACE

static inline void convertAddress(quint64 from, quint8 (&to)[6])
{
    to[0] = (from >> 0) & 0xff;
    to[1] = (from >> 8) & 0xff;
    to[2] = (from >> 16) & 0xff;
    to[3] = (from >> 24) & 0xff;
    to[4] = (from >> 32) & 0xff;
    to[5] = (from >> 40) & 0xff;
}

QBluetoothServerPrivate::QBluetoothServerPrivate(QBluetoothServer::ServerType sType)
    :   maxPendingConnections(1), serverType(sType), socketNotifier(0)
{
    if (sType == QBluetoothServer::RfcommServer)
        socket = new QBluetoothSocket(QBluetoothSocket::RfcommSocket);
    else
       socket = new QBluetoothSocket(QBluetoothSocket::L2capSocket);
}

QBluetoothServerPrivate::~QBluetoothServerPrivate()
{
    delete socketNotifier;

    delete socket;
}

void QBluetoothServerPrivate::_q_newConnection()
{
    // disable socket notifier until application calls nextPendingConnection().
    socketNotifier->setEnabled(false);

    emit q_ptr->newConnection();
}

void QBluetoothServer::close()
{
    Q_D(QBluetoothServer);

    delete d->socketNotifier;
    d->socketNotifier = 0;

    d->socket->close();
}

bool QBluetoothServer::listen(const QBluetoothAddress &address, quint16 port)
{
    Q_D(QBluetoothServer);

    int sock = d->socket->socketDescriptor();
    if (sock < 0)
        return false;

    if (d->serverType == RfcommServer) {
        sockaddr_rc addr;

        addr.rc_family = AF_BLUETOOTH;
        addr.rc_channel = port;

        if (!address.isNull())
            convertAddress(address.toUInt64(), addr.rc_bdaddr.b);
        else
            convertAddress(Q_UINT64_C(0), addr.rc_bdaddr.b);


        if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(sockaddr_rc)) < 0)
            return false;
    } else {
        sockaddr_l2 addr;

        memset(&addr, 0, sizeof(sockaddr_l2));
        addr.l2_family = AF_BLUETOOTH;
        addr.l2_psm = port;

        if (!address.isNull())
            convertAddress(address.toUInt64(), addr.l2_bdaddr.b);
        else
            convertAddress(Q_UINT64_C(0), addr.l2_bdaddr.b);

        if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(sockaddr_l2)) < 0)
            return false;
    }

    if (::listen(sock, d->maxPendingConnections) < 0)
        return false;

    d->socket->setSocketState(QBluetoothSocket::ListeningState);

    if (!d->socketNotifier) {
        d->socketNotifier = new QSocketNotifier(d->socket->socketDescriptor(),
                                                QSocketNotifier::Read);
        connect(d->socketNotifier, SIGNAL(activated(int)), this, SLOT(_q_newConnection()));
    }

    return true;
}

void QBluetoothServer::setMaxPendingConnections(int numConnections)
{
    Q_D(QBluetoothServer);

    if (d->socket->state() == QBluetoothSocket::UnconnectedState)
        d->maxPendingConnections = numConnections;
}

bool QBluetoothServer::hasPendingConnections() const
{
    Q_D(const QBluetoothServer);

    if (!d || !d->socketNotifier)
        return false;

    // if the socket notifier is disable there is a pending connection waiting for us to accept.
    return !d->socketNotifier->isEnabled();
}

QBluetoothSocket *QBluetoothServer::nextPendingConnection()
{
    Q_D(QBluetoothServer);

    if (!hasPendingConnections())
        return 0;

    int pending;
    if (d->serverType == RfcommServer) {
        sockaddr_rc addr;
        socklen_t length = sizeof(sockaddr_rc);
        pending = ::accept(d->socket->socketDescriptor(),
                               reinterpret_cast<sockaddr *>(&addr), &length);
    } else {
        sockaddr_l2 addr;
        socklen_t length = sizeof(sockaddr_l2);
        pending = ::accept(d->socket->socketDescriptor(),
                               reinterpret_cast<sockaddr *>(&addr), &length);
    }

    if (pending >= 0) {
        QBluetoothSocket *newSocket = new QBluetoothSocket;
        if (d->serverType == RfcommServer)
            newSocket->setSocketDescriptor(pending, QBluetoothSocket::RfcommSocket);
        else
            newSocket->setSocketDescriptor(pending, QBluetoothSocket::L2capSocket);

        d->socketNotifier->setEnabled(true);

        return newSocket;
    } else {
        d->socketNotifier->setEnabled(true);
    }

    return 0;
}

QBluetoothAddress QBluetoothServer::serverAddress() const
{
    Q_D(const QBluetoothServer);

    return d->socket->localAddress();
}

quint16 QBluetoothServer::serverPort() const
{
    Q_D(const QBluetoothServer);

    return d->socket->localPort();
}

void QBluetoothServer::setSecurityFlags(QBluetooth::SecurityFlags security)
{
    Q_D(QBluetoothServer);

    int lm = 0;
    if (security == QBluetooth::NoSecurity)
        lm = 0;

    if (d->serverType == RfcommServer) {
        if (security.testFlag(QBluetooth::Authorization))
            lm |= RFCOMM_LM_AUTH;
        if (security.testFlag(QBluetooth::Authentication))
            lm |= RFCOMM_LM_TRUSTED;
        if (security.testFlag(QBluetooth::Encryption))
            lm |= RFCOMM_LM_ENCRYPT;
        if (security.testFlag(QBluetooth::Secure))
            lm |= RFCOMM_LM_SECURE;

        qDebug() << hex << "Setting lm to" << lm << security;

        if (setsockopt(d->socket->socketDescriptor(), SOL_RFCOMM, RFCOMM_LM, &lm, sizeof(lm)) < 0){
            qWarning() << "Failed to set socket option, closing socket for safety" << errno;
            qWarning() << "Error: " << strerror(errno);
            d->socket->close();
        }
    } else {
        if (security.testFlag(QBluetooth::Authorization))
            lm |= L2CAP_LM_AUTH;
        if (security.testFlag(QBluetooth::Authentication))
            lm |= L2CAP_LM_TRUSTED;
        if (security.testFlag(QBluetooth::Encryption))
            lm |= L2CAP_LM_ENCRYPT;
        if (security.testFlag(QBluetooth::Secure))
            lm |= L2CAP_LM_SECURE;

        if (setsockopt(d->socket->socketDescriptor(), SOL_L2CAP, L2CAP_LM, &lm, sizeof(lm)) < 0){
            qWarning() << "Failed to set socket option, closing socket for safety" << errno;
            qWarning() << "Error: " << strerror(errno);
            d->socket->close();
        }
    }

}

QBluetooth::SecurityFlags QBluetoothServer::securityFlags() const
{
    Q_D(const QBluetoothServer);

    int lm = 0;
    int len = sizeof(lm);
    int security = QBluetooth::NoSecurity;

    if (d->serverType == RfcommServer) {
        if (getsockopt(d->socket->socketDescriptor(), SOL_RFCOMM, RFCOMM_LM, &lm, (socklen_t *)&len) < 0) {
            qWarning() << "Failed to get security flags" << strerror(errno);
            return QBluetooth::NoSecurity;
        }

        if (lm & RFCOMM_LM_SECURE)
            security |= QBluetooth::Secure;

        if (lm & RFCOMM_LM_ENCRYPT)
            security |= QBluetooth::Encryption;

        if (lm & RFCOMM_LM_TRUSTED)
            security |= QBluetooth::Authentication;

        if (lm & RFCOMM_LM_AUTH)
            security |= QBluetooth::Authorization;
    } else {
        if (getsockopt(d->socket->socketDescriptor(), SOL_L2CAP, L2CAP_LM, &lm, (socklen_t *)&len) < 0) {
            qWarning() << "Failed to get security flags" << strerror(errno);
            return QBluetooth::NoSecurity;
        }

        if (lm & L2CAP_LM_SECURE)
            security |= QBluetooth::Secure;

        if (lm & L2CAP_LM_ENCRYPT)
            security |= QBluetooth::Encryption;

        if (lm & L2CAP_LM_TRUSTED)
            security |= QBluetooth::Authentication;

        if (lm & L2CAP_LM_AUTH)
            security |= QBluetooth::Authorization;
    }

    return static_cast<QBluetooth::Security>(security);
}

QT_END_NAMESPACE
