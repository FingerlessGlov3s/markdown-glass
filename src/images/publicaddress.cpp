#include "images/publicaddress.h"

#include <QList>
#include <QPair>
#include <QString>

#include <algorithm>
#include <optional>

namespace {

// The IPv4 address carried inside an IPv6 one by the NAT64 (64:ff9b::/96)
// and 6to4 (2002::/16) prefixes, which reach it through a translator, and by
// the deprecated IPv4-compatible form (::/96).
std::optional<QHostAddress> embeddedIpv4(const QHostAddress &address)
{
    static const auto nat64 = QHostAddress::parseSubnet(QStringLiteral("64:ff9b::/96"));
    static const auto sixToFour = QHostAddress::parseSubnet(QStringLiteral("2002::/16"));
    static const auto compatible = QHostAddress::parseSubnet(QStringLiteral("::/96"));
    const Q_IPV6ADDR bytes = address.toIPv6Address();
    auto ipv4 = [&bytes](int at) {
        return QHostAddress(quint32(bytes[at]) << 24 | quint32(bytes[at + 1]) << 16 | quint32(bytes[at + 2]) << 8
                            | bytes[at + 3]);
    };
    if (address.isInSubnet(nat64) || address.isInSubnet(compatible))
        return ipv4(12);
    if (address.isInSubnet(sixToFour))
        return ipv4(2);
    return std::nullopt;
}

} // namespace

bool isPublicAddress(const QHostAddress &address)
{
    QHostAddress a = address;
    bool mapped = false;
    if (const quint32 ipv4 = a.toIPv4Address(&mapped); mapped)
        a = QHostAddress(ipv4); // ::ffff:a.b.c.d is the IPv4 address itself
    if (a.protocol() == QAbstractSocket::IPv6Protocol) {
        if (const auto inner = embeddedIpv4(a))
            return isPublicAddress(*inner);
    }
    // isGlobal() rules out loopback, link-local, multicast, broadcast,
    // unspecified and reserved addresses, but counts private ranges as global.
    if (!a.isGlobal())
        return false;
    static const QList<QPair<QHostAddress, int>> privateRanges = {
        QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
        QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
        QHostAddress::parseSubnet(QStringLiteral("100.64.0.0/10")),  // carrier-grade NAT
        QHostAddress::parseSubnet(QStringLiteral("192.0.0.0/24")),   // IETF protocol assignments
        QHostAddress::parseSubnet(QStringLiteral("198.18.0.0/15")),  // benchmarking
        QHostAddress::parseSubnet(QStringLiteral("fc00::/7")),       // unique local
        QHostAddress::parseSubnet(QStringLiteral("fec0::/10")),      // site-local
        QHostAddress::parseSubnet(QStringLiteral("64:ff9b:1::/48")), // local-use NAT64
        QHostAddress::parseSubnet(QStringLiteral("2001::/32")),      // Teredo: tunnels to a client we cannot judge
    };
    return std::ranges::none_of(privateRanges, [&a](const auto &range) { return a.isInSubnet(range); });
}
