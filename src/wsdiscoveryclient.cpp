// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019 Casper Meijn <casper@meijn.net>
// SPDX-FileCopyrightText: 2023 Harald Sitter <sitter@kde.org>

#include "wsdiscoveryclient.h"

#include "loggingcategory.h"
#include "wsdiscoverytargetservice.h"

#include <KDSoapClient/KDSoapMessage>
#include <KDSoapClient/KDSoapMessageAddressingProperties>
#include <KDSoapClient/KDSoapUdpClient>
#include <QHostAddress>
#include <QTimer>
#include <QUuid>

#include "wsdl_ws-discovery.h"

static const int DISCOVERY_PORT = 3702;
#define DISCOVERY_ADDRESS_IPV4 (QHostAddress(QStringLiteral("239.255.255.250")))
#define DISCOVERY_ADDRESS_IPV6 (QHostAddress(QStringLiteral("FF02::C")))

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) && (KDSOAP_VERSION <= KDSOAP_VERSION_CHECK(2, 1, 1))
QDebug operator<<(QDebug dbg, const KDQName &qn)
{
    if (!qn.nameSpace().isEmpty())
        dbg << "(" << qn.nameSpace() << "," << qn.localName() << ")";
    else
        dbg << qn.qname();
    return dbg;
}
#endif

WSDiscoveryClient::WSDiscoveryClient(QObject *parent)
    : QObject(parent)
    , m_soapUdpClient(new KDSoapUdpClient(this))
{
    connect(m_soapUdpClient, &KDSoapUdpClient::receivedMessage, this, &WSDiscoveryClient::receivedMessage);
}

WSDiscoveryClient::~WSDiscoveryClient() = default;

void WSDiscoveryClient::start(quint16 port)
{
    bool rc = m_soapUdpClient->bind(port, QAbstractSocket::DefaultForPlatform);
    Q_ASSERT(rc);
}

void WSDiscoveryClient::sendProbe(const QList<KDQName> &typeList, const QList<QUrl> &scopeList)
{
    WSDiscovery200504::TNS__ProbeType probe;
    if (!typeList.isEmpty()) {
        WSDiscovery200504::TNS__QNameListType types;
        types.setEntries(typeList);
        probe.setTypes(types);
    }
    if (!scopeList.isEmpty()) {
        WSDiscovery200504::TNS__UriListType scopeValues;
        scopeValues.setEntries(QUrl::toStringList(scopeList));
        WSDiscovery200504::TNS__ScopesType scopes;
        scopes.setValue(scopeValues);
        probe.setScopes(scopes);
    }

    KDSoapMessage message;
    message = probe.serialize(QStringLiteral("Probe"));
    message.setUse(KDSoapMessage::LiteralUse);
    message.setNamespaceUri(QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery"));

    KDSoapMessageAddressingProperties addressing;
    addressing.setAddressingNamespace(KDSoapMessageAddressingProperties::Addressing200408);
    addressing.setAction(QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    addressing.setMessageID(QStringLiteral("urn:uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces));
#else
    addressing.setMessageID(QStringLiteral("urn:uuid:") + QUuid::createUuid().toString().remove("{").remove("}"));
#endif
    addressing.setDestination(QStringLiteral("urn:schemas-xmlsoap-org:ws:2005:04:discovery"));
    addressing.setReplyEndpointAddress(KDSoapMessageAddressingProperties::predefinedAddressToString(KDSoapMessageAddressingProperties::Anonymous));
    message.setMessageAddressingProperties(addressing);

    const auto ipv4Sent = m_soapUdpClient->sendMessage(message, KDSoapHeaders(), DISCOVERY_ADDRESS_IPV4, DISCOVERY_PORT);
    const auto ipv6Sent = m_soapUdpClient->sendMessage(message, KDSoapHeaders(), DISCOVERY_ADDRESS_IPV6, DISCOVERY_PORT);
    if (!ipv4Sent && !ipv6Sent) {
        qCWarning(KDSoapWSDiscoveryClient) << "Message could not be sent on IPv4 or IPv6. The network may be misconfigured.";
        Q_ASSERT(false);
    }
}

void WSDiscoveryClient::sendResolve(const QString &endpointReferenceString)
{
    WSDiscovery200504::TNS__ResolveType resolve;

    WSDiscovery200504::WSA__AttributedURI endpointReferenceAddress;
    endpointReferenceAddress.setValue(endpointReferenceString);
    WSDiscovery200504::WSA__EndpointReferenceType endpointReference;
    endpointReference.setAddress(endpointReferenceAddress);
    resolve.setEndpointReference(endpointReference);

    KDSoapMessage message;
    message = resolve.serialize(QStringLiteral("Resolve"));
    message.setUse(KDSoapMessage::LiteralUse);
    message.setNamespaceUri(QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery"));

    KDSoapMessageAddressingProperties addressing;
    addressing.setAddressingNamespace(KDSoapMessageAddressingProperties::Addressing200408);
    addressing.setAction(QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery/Resolve"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    addressing.setMessageID(QStringLiteral("urn:uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces));
#else
    addressing.setMessageID(QStringLiteral("urn:uuid:") + QUuid::createUuid().toString().remove("{").remove("}"));
#endif
    addressing.setDestination(QStringLiteral("urn:schemas-xmlsoap-org:ws:2005:04:discovery"));
    addressing.setReplyEndpointAddress(KDSoapMessageAddressingProperties::predefinedAddressToString(KDSoapMessageAddressingProperties::Anonymous));
    message.setMessageAddressingProperties(addressing);

    const auto ipv4Sent = m_soapUdpClient->sendMessage(message, KDSoapHeaders(), DISCOVERY_ADDRESS_IPV4, DISCOVERY_PORT);
    const auto ipv6Sent = m_soapUdpClient->sendMessage(message, KDSoapHeaders(), DISCOVERY_ADDRESS_IPV6, DISCOVERY_PORT);
    if (!ipv4Sent && !ipv6Sent) {
        qCWarning(KDSoapWSDiscoveryClient) << "Message could not be sent on IPv4 or IPv6. The network may be misconfigured.";
        Q_ASSERT(false);
    }
}

void WSDiscoveryClient::receivedMessage(const KDSoapMessage &replyMessage,
                                        const KDSoapHeaders &replyHeaders,
                                        const QHostAddress &senderAddress,
                                        quint16 senderPort)
{
    Q_UNUSED(replyHeaders);
    Q_UNUSED(senderAddress);
    Q_UNUSED(senderPort);
    if (replyMessage.messageAddressingProperties().action() == QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe")) {
        // NO-OP
    } else if (replyMessage.messageAddressingProperties().action() == QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery/Resolve")) {
        // NO-OP
    } else if (replyMessage.messageAddressingProperties().action() == QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery/ResolveMatches")) {
        WSDiscovery200504::TNS__ResolveMatchesType resolveMatches;
        resolveMatches.deserialize(replyMessage);

        auto resolveMatch = resolveMatches.resolveMatch();
        const QString &endpointReference = resolveMatch.endpointReference().address();
        auto service = WSDiscoveryTargetService(endpointReference);
        service.setTypeList(resolveMatch.types().entries());
        service.setScopeList(QUrl::fromStringList(resolveMatch.scopes().value().entries()));
        service.setXAddrList(QUrl::fromStringList(resolveMatch.xAddrs().entries()));
        service.updateLastSeen();
        emit resolveMatchReceived(service);
    } else if (replyMessage.messageAddressingProperties().action() == QStringLiteral("http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches")) {
        WSDiscovery200504::TNS__ProbeMatchesType probeMatches;
        probeMatches.deserialize(replyMessage);

        const QList<WSDiscovery200504::TNS__ProbeMatchType> &probeMatchList = probeMatches.probeMatch();
        for (const WSDiscovery200504::TNS__ProbeMatchType &probeMatch : probeMatchList) {
            const QString &endpointReference = probeMatch.endpointReference().address();
            auto service = WSDiscoveryTargetService(endpointReference);
            service.setTypeList(probeMatch.types().entries());
            service.setScopeList(QUrl::fromStringList(probeMatch.scopes().value().entries()));
            service.setXAddrList(QUrl::fromStringList(probeMatch.xAddrs().entries()));
            service.updateLastSeen();
            emit probeMatchReceived(service);
        }
    } else {
        qCDebug(KDSoapWSDiscoveryClient) << "Received message with unknown action:" << replyMessage.messageAddressingProperties().action();
    }
}
