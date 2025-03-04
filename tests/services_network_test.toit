// Copyright (C) 2022 Toitware ApS.
// Use of this source code is governed by a Zero-Clause BSD license that can
// be found in the tests/LICENSE file.

import net
import net.impl
import net.tcp
import writer
import expect

import system.services show ServiceSelector ServiceResource
import system.api.network show NetworkService NetworkServiceClient
import system.base.network show ProxyingNetworkServiceProvider

FAKE_TAG ::= "fake-$(random 1024)"
FAKE_SELECTOR ::= NetworkService.SELECTOR.restrict.allow --tag=FAKE_TAG

service_/NetworkServiceClient? ::= (NetworkServiceClient FAKE_SELECTOR).open
    --if_absent=: null

main:
  service := FakeNetworkServiceProvider
  service.install
  test_address service
  test_resolve service
  test_tcp service
  service.uninstall

test_address service/FakeNetworkServiceProvider:
  local_address ::= net.open.address
  service.address = null
  expect.expect_equals local_address open_fake.address
  service.address = local_address.to_byte_array
  expect.expect_equals local_address open_fake.address
  service.address = #[1, 2, 3, 4]
  expect.expect_equals "1.2.3.4" open_fake.address.stringify
  service.address = #[7, 8, 9, 10]
  expect.expect_equals "7.8.9.10" open_fake.address.stringify
  service.address = null

test_resolve service/FakeNetworkServiceProvider:
  www_google ::= net.open.resolve "www.google.com"
  service.resolve = null
  expect.expect_list_equals www_google (open_fake.resolve "www.google.com")
  service.resolve = www_google.map: it.to_byte_array
  expect.expect_list_equals www_google (open_fake.resolve "www.google.com")
  service.resolve = []
  expect.expect_equals [] (open_fake.resolve "www.google.com")
  service.resolve = [#[1, 2, 3, 4]]
  expect.expect_equals [net.IpAddress #[1, 2, 3, 4]] (open_fake.resolve "www.google.com")
  service.resolve = [#[3, 4, 5, 6]]
  expect.expect_equals [net.IpAddress #[3, 4, 5, 6]] (open_fake.resolve "www.google.com")
  service.resolve = null

test_tcp service/FakeNetworkServiceProvider:
  test_tcp_network open_fake
  service.enable_tcp_proxying
  test_tcp_network open_fake
  service.disable_tcp_proxying

test_tcp_network network/net.Interface:
  socket/tcp.Socket := network.tcp_connect "www.google.com" 80
  try:
    expect.expect_equals 80 socket.peer_address.port
    expect.expect_equals network.address socket.local_address.ip

    writer := writer.Writer socket
    writer.write "GET / HTTP/1.1\r\nConnection: close\r\n\r\n"
    response := #[]
    while data := socket.read:
      response += data
    expected := "HTTP/1.1 200 OK\r\n"
    expect.expect_equals expected response[0..expected.size].to_string
  finally:
    socket.close
    network.close

// --------------------------------------------------------------------------

open_fake -> net.Interface:
  return impl.SystemInterface_ service_ service_.connect

class FakeNetworkServiceProvider extends ProxyingNetworkServiceProvider:
  proxy_mask_/int := 0
  address_/ByteArray? := null
  resolve_/List? := null

  constructor:
    super "system/network/test" --major=1 --minor=2  // Major and minor versions do not matter here.
    provides NetworkService.SELECTOR
        --handler=this
        --priority=10  // Lower than the default, so others do not find this.
        --tags=[FAKE_TAG]

  proxy_mask -> int:
    return proxy_mask_

  open_network -> net.Interface:
    return net.open

  close_network network/net.Interface -> none:
    network.close

  update_proxy_mask_ mask/int add/bool:
    if add: proxy_mask_ |= mask
    else: proxy_mask_ &= ~mask

  address= value/ByteArray?:
    update_proxy_mask_ NetworkService.PROXY_ADDRESS (value != null)
    address_ = value

  resolve= value/List?:
    update_proxy_mask_ NetworkService.PROXY_RESOLVE (value != null)
    resolve_ = value

  enable_tcp_proxying -> none:
    update_proxy_mask_ NetworkService.PROXY_TCP true
  enable_udp_proxying -> none:
    update_proxy_mask_ NetworkService.PROXY_UDP true

  disable_tcp_proxying -> none:
    update_proxy_mask_ NetworkService.PROXY_TCP false
  disable_udp_proxying -> none:
    update_proxy_mask_ NetworkService.PROXY_UDP false

  address resource/ServiceResource -> ByteArray:
    return address_

  resolve resource/ServiceResource host/string -> List:
    return resolve_
