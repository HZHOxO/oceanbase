/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "log_entry_header.h"
#include "lib/checksum/ob_crc64.h"
#include "lib/checksum/ob_parity_check.h" // parity_check
#include "lib/ob_errno.h"

namespace oceanbase
{
using namespace share;
namespace palf
{

const int64_t LogEntryHeader::HEADER_SER_SIZE = sizeof(LogEntryHeader);

LogEntryHeader::LogEntryHeader()
  : magic_(0),
    version_(0),
    log_size_(0),
    scn_(),
    data_checksum_(0),
    flag_(0)
{}

LogEntryHeader::~LogEntryHeader()
{
  reset();
}

LogEntryHeader& LogEntryHeader::operator=(const LogEntryHeader &header)
{
  magic_ = header.magic_;
  version_ = header.version_;
  log_size_ = header.log_size_;
  scn_ = header.scn_;
  data_checksum_ = header.data_checksum_;
  flag_ = header.flag_;
  return *this;
}

void LogEntryHeader::reset()
{
  magic_ = 0;
  version_ = 0;
  data_checksum_ = 0;
  log_size_ = -1;
  scn_.reset();
  flag_ = 0;
}

bool LogEntryHeader::is_valid() const
{
  return (magic_ == LogEntryHeader::MAGIC && log_size_ > 0 && scn_.is_valid());
}

bool LogEntryHeader::get_header_parity_check_res_() const
{
  bool bool_ret = parity_check(reinterpret_cast<const uint16_t &>(magic_));
  bool_ret ^= parity_check(reinterpret_cast<const uint16_t &>(version_));
  bool_ret ^= parity_check(reinterpret_cast<const uint32_t &>(log_size_));
  bool_ret ^= parity_check((scn_.get_val_for_logservice()));
  bool_ret ^= parity_check(reinterpret_cast<const uint64_t &>(data_checksum_));
  int64_t tmp_flag = (flag_ & ~(0x1));
  bool_ret ^= parity_check(reinterpret_cast<const uint64_t &>(tmp_flag));
  return bool_ret;
}

void LogEntryHeader::update_header_checksum_()
{
  if (get_header_parity_check_res_()) {
    flag_ |= 0x1;
  }
  PALF_LOG(TRACE, "update_header_checksum_ finished", K(*this), "parity flag", (flag_ & 0x1));
}

int LogEntryHeader::generate_header(const char *log_data,
                                    const int64_t data_len,
                                    const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (NULL == log_data || data_len <= 0 || !scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    magic_ = LogEntryHeader::MAGIC;
    version_ = LOG_ENTRY_HEADER_VERSION;
    log_size_ = data_len;
    scn_ = scn;
    data_checksum_ = common::ob_crc64(log_data, data_len);
    // update header checksum after all member vars assigned
    (void) update_header_checksum_();
    PALF_LOG(TRACE, "generate_header", KPC(this));
  }
  return ret;
}

bool LogEntryHeader::check_header_checksum_() const
{
  const int64_t header_checksum = get_header_parity_check_res_() ? 1 : 0;
  const int64_t saved_header_checksum = flag_ & (0x1);
  return (header_checksum == saved_header_checksum);
}

bool LogEntryHeader::check_header_integrity() const
{
  return true == is_valid() && true == check_header_checksum_();
}

bool LogEntryHeader::check_integrity(const char *buf, const int64_t data_len) const
{
  bool bool_ret = false;
  if (NULL == buf || data_len <= 0) {
    PALF_LOG(WARN, "invalid arguments", KP(buf), K(data_len));
  } else if (LogEntryHeader::MAGIC != magic_) {
    bool_ret = false;
    PALF_LOG(WARN, "magic is different", K_(magic));
  } else if (false == check_header_checksum_()) {
    PALF_LOG(WARN, "check header checsum failed", K(*this));
  } else {
    const int64_t tmp_data_checksum = common::ob_crc64(buf, data_len);
    if (data_checksum_ == tmp_data_checksum) {
      bool_ret = true;
    } else {
      bool_ret = false;
      PALF_LOG(WARN, "data checksum mismatch", K_(data_checksum), K(tmp_data_checksum), K(data_len), KPC(this));
    }
  }
  return bool_ret;
}

DEFINE_SERIALIZE(LogEntryHeader)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(NULL == buf || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::encode_i16(buf, buf_len, new_pos, magic_))
             || OB_FAIL(serialization::encode_i16(buf, buf_len, new_pos, version_))
             || OB_FAIL(serialization::encode_i32(buf, buf_len, new_pos, log_size_))
             || OB_FAIL(scn_.fixed_serialize(buf, buf_len, new_pos))
             || OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, data_checksum_))
             || OB_FAIL(serialization::encode_i64(buf, buf_len, new_pos, flag_))) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_DESERIALIZE(LogEntryHeader)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_UNLIKELY(NULL == buf || data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((OB_FAIL(serialization::decode_i16(buf, data_len, new_pos, &magic_)))
              || OB_FAIL(serialization::decode_i16(buf, data_len, new_pos, &version_))
              || OB_FAIL(serialization::decode_i32(buf, data_len, new_pos, &log_size_))
              || OB_FAIL(scn_.fixed_deserialize(buf, data_len, new_pos))
              || OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &data_checksum_))
              || OB_FAIL(serialization::decode_i64(buf, data_len, new_pos, &flag_))) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (false == check_header_integrity()) {
    ret = OB_INVALID_DATA;
  } else {
    pos = new_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(LogEntryHeader)
{
  int64_t size = 0;
  size += serialization::encoded_length_i16(magic_);
  size += serialization::encoded_length_i16(version_);
  size += serialization::encoded_length_i32(log_size_);
  size += scn_.get_fixed_serialize_size();
  size += serialization::encoded_length_i64(data_checksum_);
  size += serialization::encoded_length_i64(flag_);
  return size;
}

} // namespace palf
} // namespace oceanbase
