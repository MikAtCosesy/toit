// Copyright (C) 2019 Toitware ApS.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; version
// 2.1 only.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// The license can be found in the file `LICENSE` in the top level
// directory of this repository.

#include <mbedtls/error.h>

#include "../primitive.h"
#include "../process.h"
#include "../objects_inline.h"
#include "../resource.h"
#include "../vm.h"

#include "tls.h"
#include "x509.h"

#if !defined(TOIT_FREERTOS) || CONFIG_TOIT_CRYPTO
namespace toit {

// A simple whitespace detector tuned for PEM format that doesn't accept exotic
// whitespace characters.
static inline bool is_white_space(int c) {
  return c == ' ' or c == '\n' or c == '\r';
}

bool X509ResourceGroup::is_pem_format(const uint8* data, size_t length) {
  const char HEADER[] = "-----BEGIN ";
  const size_t HEADER_SIZE = sizeof(HEADER) - 1;  // Don't include trailing nul character.
  while (length > 0 && is_white_space(data[0])) {
    length--;
    data++;
  }
  if (length < HEADER_SIZE) return false;
  int cmp = memcmp(char_cast(data), HEADER, HEADER_SIZE);
  return cmp == 0;
}

Object* X509ResourceGroup::parse(Process* process, const uint8_t* encoded, size_t encoded_size, bool in_flash) {
  ByteArray* proxy = process->object_heap()->allocate_proxy();
  if (proxy == null) ALLOCATION_FAILED;

  uint8 checksum[Sha::HASH_LENGTH_256];
  { Sha sha256(null, 256);
    sha256.add(encoded, encoded_size);
    sha256.get(&checksum[0]);
  }

  for (Resource* it : resources()) {
    X509Certificate* other = static_cast<X509Certificate*>(it);
    if (memcmp(checksum, other->checksum(), Sha::HASH_LENGTH_256) == 0) {
      other->reference();
      proxy->set_external_address(other);
      return proxy;
    }
  }

  X509Certificate* cert = _new X509Certificate(this);
  if (!cert) MALLOC_FAILED;

  int ret;
  if (is_pem_format(encoded, encoded_size)) {
    ret = mbedtls_x509_crt_parse(cert->cert(), encoded, encoded_size);
  } else if (in_flash) {
    ret = mbedtls_x509_crt_parse_der_nocopy(cert->cert(), encoded, encoded_size);
  } else {
    ret = mbedtls_x509_crt_parse_der(cert->cert(), encoded, encoded_size);
  }
  if (ret != 0) {
    delete cert;
    return tls_error(null, process, ret);
  }

  memcpy(cert->checksum(), checksum, Sha::HASH_LENGTH_256);
  register_resource(cert);

  proxy->set_external_address(cert);
  return proxy;
}

Object* X509Certificate::common_name_or_error(Process* process) {
  const mbedtls_asn1_named_data* item = &cert_.subject;
  while (item) {
    // Find OID that corresponds to the CN (CommonName) field of the subject.
    if (item->oid.len == 3 && strncmp("\x55\x04\x03", char_cast(item->oid.p), 3) == 0) {
      return process->allocate_string_or_error(char_cast(item->val.p), item->val.len);
    }
    item = item->next;
  }
  return process->program()->null_object();
}

MODULE_IMPLEMENTATION(x509, MODULE_X509)

PRIMITIVE(init) {
  ByteArray* proxy = process->object_heap()->allocate_proxy();
  if (proxy == null) ALLOCATION_FAILED;

  X509ResourceGroup* resource_group = _new X509ResourceGroup(process);
  if (!resource_group) MALLOC_FAILED;

  proxy->set_external_address(resource_group);
  return proxy;
}

PRIMITIVE(parse) {
  ARGS(X509ResourceGroup, resource_group, Object, input);
  HeapTagScope scope(ITERATE_CUSTOM_TAGS + BIGNUM_MALLOC_TAG);

  const uint8_t* data = null;
  size_t length = 0;
  Blob blob;
  if (is_string(input)) {
    // For the PEM format, we must provide a zero-terminated string and
    // the size of the string including the termination character,
    // otherwise the parsing will fail.
    String* str = String::cast(input);
    data = reinterpret_cast<const uint8_t*>(str->as_cstr());
    length = str->length() + 1;
    // Toit strings are stored null terminated.
    ASSERT(data[length - 1] == '\0');
    if (strlen(char_cast(data)) != length - 1) INVALID_ARGUMENT;  // String with nulls in it.
  } else if (input->byte_content(process->program(), &blob, STRINGS_OR_BYTE_ARRAYS)) {
    // If we're passed a byte array or a string slice, and it's in
    // PEM format, we hope that it ends with a zero character.
    // Otherwise parsing will fail.
    data = blob.address();
    length = blob.length();
    bool is_pem = X509ResourceGroup::is_pem_format(data, length);
    if (is_pem && (length < 1 || data[length - 1] != '\0')) INVALID_ARGUMENT;
  } else {
    WRONG_TYPE;
  }
  bool in_flash = HeapObject::cast(input)->on_program_heap(process);
  return resource_group->parse(process, data, length, in_flash);
}

PRIMITIVE(get_common_name) {
  ARGS(X509Certificate, cert);
  return cert->common_name_or_error(process);
}

PRIMITIVE(close) {
  ARGS(X509Certificate, cert);
  if (cert->dereference()) {
    cert->resource_group()->unregister_resource(cert);
  }
  cert_proxy->clear_external_address();
  return process->program()->null_object();
}


} // namespace toit
#endif // !defined(TOIT_FREERTOS) || CONFIG_TOIT_CRYPTO
