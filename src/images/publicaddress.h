#pragma once

#include <QHostAddress>

// Which addresses a document may make the viewer connect to. Remote images
// are only ever fetched from the public internet, so a document cannot probe
// this machine or the local network.

// True for an address on the public internet: not loopback, private,
// link-local, carrier-grade NAT, tunnelled or otherwise reserved, including
// when written as an IPv4-mapped or translated IPv6 address.
bool isPublicAddress(const QHostAddress &address);
