////////////////////////////////////////////////////////////////////////////////
/// @brief datafiles
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2011 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2011, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "datafile.h"

#include <BasicsC/hashes.h>
#include <BasicsC/logging.h>
#include <BasicsC/memory-map.h>
#include <BasicsC/strings.h>
#include <BasicsC/files.h>

// #define DEBUG_DATAFILE 1

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a datafile
////////////////////////////////////////////////////////////////////////////////

static void InitDatafile (TRI_datafile_t* datafile,
                          char* filename,
                          int fd,
                          void* mmHandle,
                          TRI_voc_size_t maximalSize,
                          TRI_voc_size_t currentSize,
                          TRI_voc_fid_t tick,
                          char* data) {

  datafile->_state = TRI_DF_STATE_READ;
  datafile->_fid = tick;

  datafile->_filename = filename;
  datafile->_fd = fd;
  datafile->_mmHandle = mmHandle;

  datafile->_maximalSize = maximalSize;
  datafile->_currentSize = currentSize;
  datafile->_footerSize = sizeof(TRI_df_footer_marker_t);

  datafile->_isSealed = false;
  datafile->_lastError = TRI_ERROR_NO_ERROR;

  datafile->_full = false;

  datafile->_data = data;
  datafile->_next = data + currentSize;

  datafile->_synced = data;
  datafile->_nSynced = 0;
  datafile->_lastSynced = 0;

  datafile->_written = NULL;
  datafile->_nWritten = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief truncates a datafile
///
/// Create a truncated datafile, seal it and rename the old.
////////////////////////////////////////////////////////////////////////////////

static int TruncateDatafile (TRI_datafile_t* datafile, TRI_voc_size_t vocSize) {
  char* filename;
  char* oldname;
  char zero;
  int fd;
  int res;
  size_t maximalSize;
  size_t offset;
  void* data;
  void* mmHandle;
  
  // use multiples of page-size
  maximalSize = ((vocSize + sizeof(TRI_df_footer_marker_t) + PageSize - 1) / PageSize) * PageSize;

  // sanity check
  if (sizeof(TRI_df_header_marker_t) + sizeof(TRI_df_footer_marker_t) > maximalSize) {
    LOG_ERROR("cannot create datafile '%s', maximal size '%u' is too small", datafile->_filename, (unsigned int) maximalSize);
    return TRI_set_errno(TRI_ERROR_ARANGO_MAXIMAL_SIZE_TOO_SMALL);
  }

  // open the file
  filename = TRI_Concatenate2String(datafile->_filename, ".new");

  fd = TRI_CREATE(filename, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);

  if (fd < 0) {
    LOG_ERROR("cannot create datafile '%s': '%s'", filename, TRI_last_error());
    return TRI_set_errno(TRI_ERROR_SYS_ERROR);
  }

  // create sparse file
  offset = lseek(fd, maximalSize - 1, SEEK_SET);

  if (offset == (off_t) -1) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    close(fd);

    // remove empty file
    TRI_UnlinkFile(filename);

    LOG_ERROR("cannot seek in datafile '%s': '%s'", filename, TRI_last_error());
    return TRI_ERROR_SYS_ERROR;
  }

  zero = 0;
  res = write(fd, &zero, 1);

  if (res < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    close(fd);
    
    // remove empty file
    TRI_UnlinkFile(filename);

    LOG_ERROR("cannot create sparse datafile '%s': '%s'", filename, TRI_last_error());
    return TRI_ERROR_SYS_ERROR;
  }

  // memory map the data
  res = TRI_MMFile(0, maximalSize, PROT_WRITE | PROT_READ, MAP_SHARED, &fd, &mmHandle, 0, &data);
  
  if (res != TRI_ERROR_NO_ERROR) {  
    TRI_set_errno(res);
    close(fd);

    // remove empty file
    TRI_UnlinkFile(filename);

    LOG_ERROR("cannot memory map file '%s': '%s'", filename, TRI_last_error());
    return TRI_errno();
  }

  // copy the data
  memcpy(data, datafile->_data, vocSize);

  // patch the datafile structure
  res = TRI_UNMMFile(datafile->_data, datafile->_maximalSize, &(datafile->_fd), &(datafile->_mmHandle));

  if (res < 0) {
    LOG_ERROR("munmap failed with: %d", res);
    return res;
  }

  close(datafile->_fd);
  // the datafile->_mmHandle object has been closed in the underlying TRI_UNMMFile(...) call above
  
  datafile->_data = data;
  datafile->_next = (char*)(data) + vocSize;
  datafile->_maximalSize = maximalSize;
  datafile->_fd = fd;
  datafile->_mmHandle = mmHandle;
  
  // rename files
  oldname = TRI_Concatenate2String(datafile->_filename, ".corrupted");

  res = TRI_RenameFile(datafile->_filename, oldname);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, filename);
    TRI_FreeString(TRI_CORE_MEM_ZONE, oldname);

    return res;
  }

  res = TRI_RenameFile(filename, datafile->_filename);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, filename);
    TRI_FreeString(TRI_CORE_MEM_ZONE, oldname);

    return res;
  }

  TRI_SealDatafile(datafile);
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief scans a datafile
////////////////////////////////////////////////////////////////////////////////

static TRI_df_scan_t ScanDatafile (TRI_datafile_t const* datafile) {
  TRI_df_scan_t scan;
  TRI_df_scan_entry_t entry;

  TRI_voc_size_t currentSize;
  char* end;
  char* ptr;

  ptr = datafile->_data;
  end = datafile->_data + datafile->_currentSize;
  currentSize = 0;

  TRI_InitVector(&scan._entries, TRI_CORE_MEM_ZONE, sizeof(TRI_df_scan_entry_t));

  scan._currentSize = datafile->_currentSize;
  scan._maximalSize = datafile->_maximalSize;
  scan._numberMarkers = 0;
  scan._status = 1;

  if (datafile->_currentSize == 0) {
    end = datafile->_data + datafile->_maximalSize;
  }

  while (ptr < end) {
    TRI_df_marker_t* marker = (TRI_df_marker_t*) ptr;
    bool ok;
    size_t size;

    memset(&entry, 0, sizeof(entry));

    entry._position = ptr - datafile->_data;
    entry._size = marker->_size;
    entry._tick = marker->_tick;
    entry._type = marker->_type;
    entry._status = 1;

    if (marker->_size == 0 && marker->_crc == 0 && marker->_type == 0 && marker->_tick == 0) {
      entry._status = 2;

      scan._endPosition = currentSize;

      TRI_PushBackVector(&scan._entries, &entry);
      return scan;
    }

    ++scan._numberMarkers;

    if (marker->_size == 0) {
      entry._status = 3;

      scan._status = 2;
      scan._endPosition = currentSize;

      TRI_PushBackVector(&scan._entries, &entry);
      return scan;
    }

    if (marker->_size < sizeof(TRI_df_marker_t)) {
      entry._status = 4;

      scan._endPosition = currentSize;
      scan._status = 3;

      TRI_PushBackVector(&scan._entries, &entry);
      return scan;
    }

    ok = TRI_CheckCrcMarkerDatafile(marker);

    if (! ok) {
      entry._status = 5;
      scan._status = 4;
    }

    TRI_PushBackVector(&scan._entries, &entry);

    size = ((marker->_size + TRI_DF_BLOCK_ALIGN - 1) / TRI_DF_BLOCK_ALIGN) * TRI_DF_BLOCK_ALIGN;
    currentSize += size;

    if (marker->_type == TRI_DF_MARKER_FOOTER) {
      scan._endPosition = currentSize;
      return scan;
    }

    ptr += size;
  }

  return scan;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks a datafile
////////////////////////////////////////////////////////////////////////////////

static bool CheckDatafile (TRI_datafile_t* datafile) {
  TRI_voc_size_t currentSize;
  char* end;
  char* ptr;

  ptr = datafile->_data;
  end = datafile->_data + datafile->_currentSize;
  currentSize = 0;

  if (datafile->_currentSize == 0) {
    LOG_WARNING("current size is 0 in read-only datafile '%s', trying to fix",
                datafile->_filename);

    end = datafile->_data + datafile->_maximalSize;
  }

  while (ptr < end) {
    TRI_df_marker_t* marker = (TRI_df_marker_t*) ptr;
    bool ok;
    size_t size;

#ifdef DEBUG_DATAFILE
    LOG_TRACE("MARKER: size %lu, tick %lx, crc %lx, type %u",
              (unsigned long) marker->_size,
              (unsigned long) marker->_tick,
              (unsigned long) marker->_crc,
              (unsigned int) marker->_type);
#endif

    if (marker->_size == 0 && marker->_crc == 0 && marker->_type == 0 && marker->_tick == 0) {
      LOG_DEBUG("reached end of datafile '%s' data, current size %lu",
               datafile->_filename,
               (unsigned long) currentSize);

      datafile->_currentSize = currentSize;
      datafile->_next = datafile->_data + datafile->_currentSize;

      return true;
    }

    if (marker->_size == 0) {
      LOG_DEBUG("reached end of datafile '%s' data, current size %lu",
               datafile->_filename,
               (unsigned long) currentSize);

      datafile->_currentSize = currentSize;
      datafile->_next = datafile->_data + datafile->_currentSize;

      return true;
    }

    if (marker->_size < sizeof(TRI_df_marker_t)) {
      datafile->_lastError = TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_DATAFILE);
      datafile->_currentSize = currentSize;
      datafile->_next = datafile->_data + datafile->_currentSize;
      datafile->_state = TRI_DF_STATE_OPEN_ERROR;

      LOG_WARNING("marker in datafile '%s' too small, size %lu, should be at least %lu",
                  datafile->_filename,
                  (unsigned long) marker->_size,
                  (unsigned long) sizeof(TRI_df_marker_t));

      return false;
    }

    ok = TRI_CheckCrcMarkerDatafile(marker);

    if (! ok) {
      datafile->_lastError = TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_DATAFILE);
      datafile->_currentSize = currentSize;
      datafile->_next = datafile->_data + datafile->_currentSize;
      datafile->_state = TRI_DF_STATE_OPEN_ERROR;

      LOG_WARNING("crc mismatch found in datafile '%s'", datafile->_filename);

      return false;
    }

    TRI_UpdateTickVocBase(marker->_tick);

    size = ((marker->_size + TRI_DF_BLOCK_ALIGN - 1) / TRI_DF_BLOCK_ALIGN) * TRI_DF_BLOCK_ALIGN;
    currentSize += size;

    if (marker->_type == TRI_DF_MARKER_FOOTER) {
      LOG_DEBUG("found footer, reached end of datafile '%s', current size %lu",
                datafile->_filename,
                (unsigned long) currentSize);

      datafile->_isSealed = true;
      datafile->_currentSize = currentSize;
      datafile->_next = datafile->_data + datafile->_currentSize;

      return true;
    }

    ptr += size;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief opens a datafile
////////////////////////////////////////////////////////////////////////////////

static TRI_datafile_t* OpenDatafile (char const* filename, bool ignoreErrors) {
  TRI_datafile_t* datafile;
  TRI_voc_size_t size;
  bool ok;
  void* data;
  char* ptr;
  int fd;
  int res;
  ssize_t len;
  struct stat status;
  TRI_df_header_marker_t header;
  void* mmHandle;
  
  // open the file
  fd = TRI_OPEN(filename, O_RDWR);

  if (fd < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);

    LOG_ERROR("cannot open datafile '%s': '%s'", filename, TRI_last_error());

    return NULL;
  }

  // compute the size of the file
  res = fstat(fd, &status);

  if (res < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    close(fd);

    LOG_ERROR("cannot get status of datafile '%s': %s", filename, TRI_last_error());

    return NULL;
  }

  // check that file is not too small
  size = status.st_size;

  if (size < sizeof(TRI_df_header_marker_t) + sizeof(TRI_df_footer_marker_t)) {
    TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_DATAFILE);
    close(fd);

    LOG_ERROR("datafile '%s' is corrupted, size is only %u", filename, (unsigned int) size);

    return NULL;
  }

  // read header from file
  ptr = (char*) &header;
  len = sizeof(TRI_df_header_marker_t);

  ok = TRI_ReadPointer(fd, ptr, len);

  if (! ok) {
    LOG_ERROR("cannot read datafile header from '%s': %s", filename, TRI_last_error());

    close(fd);
    return NULL;
  }

  // check CRC
  ok = TRI_CheckCrcMarkerDatafile(&header.base);

  if (! ok) {
    TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_DATAFILE);

    LOG_ERROR("corrupted datafile header read from '%s'", filename);

    if (! ignoreErrors) {
      close(fd);
      return NULL;
    }
  }

  // check the datafile version
  if (ok) {
    if (header._version != TRI_DF_VERSION) {
      TRI_set_errno(TRI_ERROR_ARANGO_CORRUPTED_DATAFILE);

      LOG_ERROR("unknown datafile version '%u' in datafile '%s'",
                (unsigned int) header._version,
                filename);

      if (! ignoreErrors) {
        close(fd);
        return NULL;
      }
    }
  }

  // check the maximal size
  if (size > header._maximalSize) {
    LOG_WARNING("datafile has size '%u', but maximal size is '%u'",
                (unsigned int) size,
                (unsigned int) header._maximalSize);
  }

  // map datafile into memory
  res = TRI_MMFile(0, size, PROT_READ, MAP_SHARED, &fd, &mmHandle, 0, &data);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(res);
    close(fd);
    LOG_ERROR("cannot memory map file '%s': '%d'", filename, res);
    return NULL;
  }

  // create datafile structure
  datafile = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_datafile_t), false);

  if (!datafile) {
    return NULL;
  }

  InitDatafile(datafile,
               TRI_DuplicateString(filename),
               fd,
               mmHandle,
               size,
               size,
               header._fid,
               data);

  return datafile;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new datafile
////////////////////////////////////////////////////////////////////////////////

TRI_datafile_t* TRI_CreateDatafile (char const* filename, TRI_voc_size_t maximalSize) {
  TRI_datafile_t* datafile;
  TRI_df_header_marker_t header;
  TRI_df_marker_t* position;
  TRI_voc_tick_t tick;
  char zero;
  int fd;
  int result;
  off_t offset;
  ssize_t res;
  void* data;
  void* mmHandle;
  
  // use multiples of page-size
  maximalSize = ((maximalSize + PageSize - 1) / PageSize) * PageSize;

  // sanity check
  if (sizeof(TRI_df_header_marker_t) + sizeof(TRI_df_footer_marker_t) > maximalSize) {
    TRI_set_errno(TRI_ERROR_ARANGO_MAXIMAL_SIZE_TOO_SMALL);

    LOG_ERROR("cannot create datafile '%s', maximal size '%u' is too small", filename, (unsigned int) maximalSize);
    return NULL;
  }

  // open the file
  fd = TRI_CREATE(filename, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);

  if (fd < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);

    LOG_ERROR("cannot create datafile '%s': '%s'", filename, TRI_last_error());
    return NULL;
  }

  // create sparse file
  offset = lseek(fd, maximalSize - 1, SEEK_SET);

  if (offset == (off_t) -1) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    close(fd);

    // remove empty file
    TRI_UnlinkFile(filename);

    LOG_ERROR("cannot seek in datafile '%s': '%s'", filename, TRI_last_error());
    return NULL;
  }

  zero = 0;
  res = write(fd, &zero, 1);

  if (res < 0) {
    TRI_set_errno(TRI_ERROR_SYS_ERROR);
    close(fd);
    
    // remove empty file
    TRI_UnlinkFile(filename);

    LOG_ERROR("cannot create sparse datafile '%s': '%s'", filename, TRI_last_error());
    return NULL;
  }

  // memory map the data
  res = TRI_MMFile(0, maximalSize, PROT_WRITE | PROT_READ, MAP_SHARED, &fd, &mmHandle, 0, &data);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(res);
    close(fd);

    // remove empty file
    TRI_UnlinkFile(filename);

    LOG_ERROR("cannot memory map file '%s': '%d'", filename, (int) res);
    return NULL;
  }

  // get next tick
  tick = TRI_NewTickVocBase();

  // create datafile structure
  datafile = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_datafile_t), false);

  if (datafile == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    close(fd);

    LOG_ERROR("out-of-memory");
    return NULL;
  }

  InitDatafile(datafile,
               TRI_DuplicateString(filename),
               fd,
               mmHandle,
               maximalSize,
               0,
               tick,
               data);

  datafile->_state = TRI_DF_STATE_WRITE;

  // create the header
  memset(&header, 0, sizeof(TRI_df_header_marker_t));

  header.base._size = sizeof(TRI_df_header_marker_t);
  header.base._tick = TRI_NewTickVocBase();
  header.base._type = TRI_DF_MARKER_HEADER;

  header._version = TRI_DF_VERSION;
  header._maximalSize = maximalSize;
  header._fid = tick;

  // create CRC
  TRI_FillCrcMarkerDatafile(&header.base, sizeof(TRI_df_header_marker_t), 0, 0, 0, 0);

  // reserve space and write header to file
  result = TRI_ReserveElementDatafile(datafile, header.base._size, &position);

  if (result == TRI_ERROR_NO_ERROR) {
    result = TRI_WriteElementDatafile(datafile, position, &header.base, header.base._size, 0, 0, 0, 0, true);
  }

  if (result != TRI_ERROR_NO_ERROR) {
    LOG_ERROR("cannot write header to datafile '%s'",
              filename);
    TRI_UNMMFile(datafile->_data, datafile->_maximalSize, &fd, &mmHandle);
    close(fd);

    TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, datafile->_filename);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, datafile);

    return NULL;
  }

  LOG_DEBUG("created datafile '%s' of size %u and page-size %u",
            filename,
            (unsigned int) maximalSize,
            (unsigned int) PageSize);

  return datafile;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyDatafile (TRI_datafile_t* datafile) {
  TRI_FreeString(TRI_CORE_MEM_ZONE, datafile->_filename);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and but frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeDatafile (TRI_datafile_t* datafile) {
  TRI_DestroyDatafile(datafile);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, datafile);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief checks a CRC of a marker
////////////////////////////////////////////////////////////////////////////////

bool TRI_CheckCrcMarkerDatafile (TRI_df_marker_t const* marker) {
  TRI_voc_size_t zero;
  char const* ptr;
  off_t o;
  size_t n;
  TRI_voc_crc_t crc;

  zero = 0;
  o = offsetof(TRI_df_marker_t, _crc);
  n = sizeof(TRI_voc_crc_t);

  ptr = (char const*) marker;

  if (marker->_size < sizeof(TRI_df_marker_t)) {
    return false;
  }

  crc = TRI_InitialCrc32();

  crc = TRI_BlockCrc32(crc, ptr, o);
  crc = TRI_BlockCrc32(crc, (char*) &zero, n);
  crc = TRI_BlockCrc32(crc, ptr + o + n, marker->_size - o - n);

  crc = TRI_FinalCrc32(crc);

  return marker->_crc == crc;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a CRC and writes that into the header
////////////////////////////////////////////////////////////////////////////////

void TRI_FillCrcMarkerDatafile (TRI_df_marker_t* marker,
                                TRI_voc_size_t markerSize,
                                void const* keyBody,
                                TRI_voc_size_t keyBodySize,
                                void const* body,
                                TRI_voc_size_t bodySize) {
  TRI_voc_crc_t crc;

  marker->_crc = 0;

  crc = TRI_InitialCrc32();
  crc = TRI_BlockCrc32(crc, (char const*) marker, markerSize);

  if (keyBody != NULL && 0 < keyBodySize) {
    crc = TRI_BlockCrc32(crc, keyBody, keyBodySize);
  }

  if (body != NULL && 0 < bodySize) {
    crc = TRI_BlockCrc32(crc, body, bodySize);
  }

  marker->_crc = TRI_FinalCrc32(crc);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a CRC and writes that into the header
////////////////////////////////////////////////////////////////////////////////

void TRI_FillCrcKeyMarkerDatafile (TRI_df_marker_t* marker,
                                TRI_voc_size_t markerSize,
                                void const* keyBody,
                                TRI_voc_size_t keyBodySize,
                                void const* body,
                                TRI_voc_size_t bodySize) {
  TRI_voc_crc_t crc;

  marker->_crc = 0;

  crc = TRI_InitialCrc32();
  crc = TRI_BlockCrc32(crc, (char const*) marker, markerSize);

  if (keyBody != NULL && 0 < keyBodySize) {
    crc = TRI_BlockCrc32(crc, keyBody, keyBodySize);
  }

  if (body != NULL && 0 < bodySize) {
    crc = TRI_BlockCrc32(crc, body, bodySize);
  }

  marker->_crc = TRI_FinalCrc32(crc);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reserves room for an element, advances the pointer
////////////////////////////////////////////////////////////////////////////////

int TRI_ReserveElementDatafile (TRI_datafile_t* datafile,
                                TRI_voc_size_t size,
                                TRI_df_marker_t** position) {
  *position = 0;
  size = ((size + TRI_DF_BLOCK_ALIGN - 1) / TRI_DF_BLOCK_ALIGN) * TRI_DF_BLOCK_ALIGN;

  if (datafile->_state != TRI_DF_STATE_WRITE) {
    if (datafile->_state == TRI_DF_STATE_READ) {
      LOG_ERROR("cannot reserve marker, datafile is read-only");

      return TRI_set_errno(TRI_ERROR_ARANGO_READ_ONLY);
    }

    return TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_STATE);
  }

  // check the maximal size
  if (size + TRI_JOURNAL_OVERHEAD > datafile->_maximalSize) {
    return TRI_set_errno(TRI_ERROR_ARANGO_DOCUMENT_TOO_LARGE);
  }

  // add the marker, leave enough room for the footer
  if (datafile->_currentSize + size + datafile->_footerSize > datafile->_maximalSize) {
    datafile->_lastError = TRI_set_errno(TRI_ERROR_ARANGO_DATAFILE_FULL);
    datafile->_full = true;

    LOG_TRACE("cannot write marker, not enough space");

    return datafile->_lastError;
  }

  *position = (TRI_df_marker_t*) datafile->_next;

  datafile->_next += size;
  datafile->_currentSize += size;

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief writes a marker and body to the datafile
////////////////////////////////////////////////////////////////////////////////

int TRI_WriteElementDatafile (TRI_datafile_t* datafile,
                              void* position,
                              TRI_df_marker_t const* marker,
                              TRI_voc_size_t markerSize,
                              void const* keyBody,
                              TRI_voc_size_t keyBodySize,
                              void const* body,
                              TRI_voc_size_t bodySize,
                              bool forceSync) {
  TRI_voc_size_t size;

  size = markerSize + keyBodySize + bodySize;

  if (size != marker->_size) {
    LOG_ERROR("marker size is %lu, but size is %lu",
              (unsigned long) marker->_size,
              (unsigned long) size);
  }

  if (datafile->_state != TRI_DF_STATE_WRITE) {
    if (datafile->_state == TRI_DF_STATE_READ) {
      LOG_ERROR("cannot write marker, datafile is read-only");

      return TRI_set_errno(TRI_ERROR_ARANGO_READ_ONLY);
    }

    return TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_STATE);
  }

  memcpy(position, marker, markerSize);

  if (keyBody != NULL && 0 < keyBodySize) {
    memcpy(((char*) position) + markerSize, keyBody, keyBodySize);
  }

  if (body != NULL && 0 < bodySize) {
    memcpy(((char*) position) + markerSize + keyBodySize, body, bodySize);
  }

  if (forceSync) {
    bool ok;

    ok = TRI_msync(datafile->_fd, datafile->_mmHandle, position, ((char*) position) + size);

    if (! ok) {
      datafile->_state = TRI_DF_STATE_WRITE_ERROR;

      if (errno == ENOSPC) {
        datafile->_lastError = TRI_set_errno(TRI_ERROR_ARANGO_FILESYSTEM_FULL);
      }
      else {
        datafile->_lastError = TRI_set_errno(TRI_ERROR_SYS_ERROR);
      }

      LOG_ERROR("msync failed with: %s", TRI_last_error());

      return datafile->_lastError;
    }
    else {
      LOG_TRACE("msync succeeded %p, size %lu", position, (unsigned long) size);
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief iterates over a datafile
////////////////////////////////////////////////////////////////////////////////

bool TRI_IterateDatafile (TRI_datafile_t* datafile,
                          bool (*iterator)(TRI_df_marker_t const*, void*, TRI_datafile_t*, bool),
                          void* data,
                          bool journal) {
  char* ptr;
  char* end;

  ptr = datafile->_data;
  end = datafile->_data + datafile->_currentSize;

  if (datafile->_state != TRI_DF_STATE_READ && datafile->_state != TRI_DF_STATE_WRITE) {
    TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_STATE);
    return false;
  }

  while (ptr < end) {
    TRI_df_marker_t* marker = (TRI_df_marker_t*) ptr;
    bool result;
    size_t size;

    if (marker->_size == 0) {
      return true;
    }

    result = iterator(marker, data, datafile, journal);

    if (! result) {
      return false;
    }

    size = ((marker->_size + TRI_DF_BLOCK_ALIGN - 1) / TRI_DF_BLOCK_ALIGN) * TRI_DF_BLOCK_ALIGN;
    ptr += size;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief opens an existing datafile
///
/// The datafile will be opened read-only if a footer is found
////////////////////////////////////////////////////////////////////////////////

TRI_datafile_t* TRI_OpenDatafile (char const* filename) {
  TRI_datafile_t* datafile;
  bool ok;

  datafile = OpenDatafile(filename, false);

  if (datafile == NULL) {
    return NULL;
  }

  // check the current marker
  ok = CheckDatafile(datafile);

  if (!ok) {
    TRI_UNMMFile(datafile->_data, datafile->_maximalSize, &(datafile->_fd), &(datafile->_mmHandle));
    close(datafile->_fd);

    LOG_ERROR("datafile '%s' is corrupt", datafile->_filename);
    // must free datafile here
    TRI_FreeDatafile(datafile);

    return NULL;
  }

  // change to read-write if no footer has been found
  if (! datafile->_isSealed) {
    datafile->_state = TRI_DF_STATE_WRITE;
    TRI_ProtectMMFile(datafile->_data, datafile->_maximalSize, PROT_READ | PROT_WRITE, &(datafile->_fd), &(datafile->_mmHandle)); 
  }

  return datafile;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief opens an existing, possible corrupt datafile
////////////////////////////////////////////////////////////////////////////////

TRI_datafile_t* TRI_ForcedOpenDatafile (char const* filename) {
  TRI_datafile_t* datafile;
  bool ok;

  datafile = OpenDatafile(filename, true);

  if (datafile == NULL) {
    return NULL;
  }

  // check the current marker
  ok = CheckDatafile(datafile);

  if (! ok) {
    LOG_ERROR("datafile '%s' is corrupt", datafile->_filename);
  }

  // change to read-write if no footer has been found
  else {
    if (! datafile->_isSealed) {
      datafile->_state = TRI_DF_STATE_WRITE;
      TRI_ProtectMMFile(datafile->_data, datafile->_maximalSize, PROT_READ | PROT_WRITE, &(datafile->_fd), &(datafile->_mmHandle)); 
    }
  }

  return datafile;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief closes a datafile and all memory regions
////////////////////////////////////////////////////////////////////////////////

bool TRI_CloseDatafile (TRI_datafile_t* datafile) {
  if (datafile->_state == TRI_DF_STATE_READ || datafile->_state == TRI_DF_STATE_WRITE) {
    int res;

    res = TRI_UNMMFile(datafile->_data, datafile->_maximalSize, &(datafile->_fd), &(datafile->_mmHandle));

    if (res != TRI_ERROR_NO_ERROR) {
      LOG_ERROR("munmap failed with: %d", res);
      datafile->_state = TRI_DF_STATE_WRITE_ERROR;
      datafile->_lastError = res;
      return false;
    }
    
    else {
      close(datafile->_fd);

      datafile->_state = TRI_DF_STATE_CLOSED;
      datafile->_data = 0;
      datafile->_next = 0;
      datafile->_fd = -1;

      return true;
    }
  }
  else if (datafile->_state == TRI_DF_STATE_CLOSED) {
    LOG_WARNING("closing a already closed datafile '%s'", datafile->_filename);
    return true;
  }
  else {
    TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_STATE);
    return false;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief renames a datafile
////////////////////////////////////////////////////////////////////////////////

bool TRI_RenameDatafile (TRI_datafile_t* datafile, char const* filename) {
  int res;

  if (TRI_ExistsFile(filename)) {
    LOG_ERROR("cannot overwrite datafile '%s'", filename);

    datafile->_lastError = TRI_set_errno(TRI_ERROR_ARANGO_DATAFILE_ALREADY_EXISTS);
    return false;
  }

  res = TRI_RenameFile(datafile->_filename, filename);

  if (res != TRI_ERROR_NO_ERROR) {
    datafile->_state = TRI_DF_STATE_RENAME_ERROR;
    datafile->_lastError = TRI_set_errno(TRI_ERROR_SYS_ERROR);

    return false;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, datafile->_filename);
  datafile->_filename = TRI_DuplicateString(filename);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief seals a database, writes a footer, sets it to read-only
////////////////////////////////////////////////////////////////////////////////

int TRI_SealDatafile (TRI_datafile_t* datafile) {
  TRI_df_footer_marker_t footer;
  TRI_df_marker_t* position;
  bool ok;
  int res;

  if (datafile->_state == TRI_DF_STATE_READ) {
    return TRI_set_errno(TRI_ERROR_ARANGO_READ_ONLY);
  }

  if (datafile->_state != TRI_DF_STATE_WRITE) {
    return TRI_set_errno(TRI_ERROR_ARANGO_ILLEGAL_STATE);
  }

  if (datafile->_isSealed) {
    return TRI_set_errno(TRI_ERROR_ARANGO_DATAFILE_SEALED);
  }

  // create the footer
  memset(&footer, 0, sizeof(TRI_df_footer_marker_t));

  footer.base._size = sizeof(TRI_df_footer_marker_t);
  footer.base._tick = TRI_NewTickVocBase();
  footer.base._type = TRI_DF_MARKER_FOOTER;

  // create CRC
  TRI_FillCrcMarkerDatafile(&footer.base, sizeof(TRI_df_footer_marker_t), 0, 0, 0, 0);

  // reserve space and write footer to file
  datafile->_footerSize = 0;

  res = TRI_ReserveElementDatafile(datafile, footer.base._size, &position);

  if (res == TRI_ERROR_NO_ERROR) {
    res = TRI_WriteElementDatafile(datafile, position, &footer.base, footer.base._size, 0, 0, 0, 0, true);
  }

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  // sync file
  ok = TRI_msync(datafile->_fd, datafile->_mmHandle, datafile->_data, ((char*) datafile->_data) + datafile->_currentSize);

  if (! ok) {
    datafile->_state = TRI_DF_STATE_WRITE_ERROR;

    if (errno == ENOSPC) {
      datafile->_lastError = TRI_set_errno(TRI_ERROR_ARANGO_FILESYSTEM_FULL);
    }
    else {
      datafile->_lastError = TRI_errno();
    }

    LOG_ERROR("msync failed with: %s", TRI_last_error());
  }

  // everything is now syned
  datafile->_synced = datafile->_written;
  datafile->_nSynced = datafile->_nWritten;

  // change the datafile to read-only
  TRI_ProtectMMFile(datafile->_data, datafile->_maximalSize, PROT_READ, &(datafile->_fd), &(datafile->_mmHandle)); 

  // truncate datafile
  if (ok) {
    res = ftruncate(datafile->_fd, datafile->_currentSize);

    if (res < 0) {
      LOG_ERROR("cannot truncate datafile '%s': %s", datafile->_filename, TRI_last_error());
      datafile->_lastError = TRI_set_errno(TRI_ERROR_SYS_ERROR);
      ok = false;
    }

    datafile->_isSealed = true;
    datafile->_state = TRI_DF_STATE_READ;
  }

  return ok ? TRI_ERROR_NO_ERROR : datafile->_lastError;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief truncates a datafile and seals it
////////////////////////////////////////////////////////////////////////////////

int TRI_TruncateDatafile (char const* path, TRI_voc_size_t position) {
  TRI_datafile_t* datafile;
  int res;

  datafile = OpenDatafile(path, true);  

  if (datafile == NULL) {
    return TRI_ERROR_ARANGO_DATAFILE_UNREADABLE;
  }

  res = TruncateDatafile(datafile, position);
  TRI_CloseDatafile(datafile);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns information about the datafile
////////////////////////////////////////////////////////////////////////////////

TRI_df_scan_t TRI_ScanDatafile (char const* path) {
  TRI_df_scan_t scan;
  TRI_datafile_t* datafile;

  datafile = OpenDatafile(path, true);  

  if (datafile != 0) {
    scan = ScanDatafile(datafile);
    TRI_CloseDatafile(datafile);
  }
  else {
    scan._currentSize = 0;
    scan._maximalSize = 0;
    scan._endPosition = 0;
    scan._numberMarkers = 0;

    TRI_InitVector(&scan._entries, TRI_CORE_MEM_ZONE, sizeof(TRI_df_scan_entry_t));

    scan._status = 5;
  }

  return scan;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys information about the datafile
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyDatafileScan (TRI_df_scan_t* scan) {
  TRI_DestroyVector(&scan->_entries);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
