#include "Audio/SoundManager.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace Microsoft::WRL;

namespace {

inline std::wstring ToWide(const std::string &s) {
  if (s.empty()) {
    return {};
  }

  const int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
  assert(len > 0);

  std::wstring ws(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, ws.data(), len);

  ws.pop_back();
  return ws;
}

#pragma pack(push, 1)
// RIFFヘッダチャンク
// struct RiffHeader {
//  ChunkHeader chunk; //"RIFF"
//  char type[4];      //"WAVE"
//};

// struct RiffHeader {
//   char chunkId[4]; // "RIFF"
//   uint32_t size;   // file size - 8
//   char format[4];  // "WAVE"
// };
//
//// チャンクヘッダ
// struct ChunkHeader {
//   char id[4];   // チャンク毎のID
//   int32_t size; // チャンクサイズ
// };

#pragma pack(pop)

// inline bool FourCCEquals(const char a[4], const char b[4]) {
//   return std::memcmp(a, b, 4) == 0;
// }

// WAVのチャンクは2バイト境界
// inline uint32_t Align2(uint32_t n) { return (n + 1u) & ~1u; }

// SoundManager::SoundData LoadWavInternal(const char *filename) {
//   std::ifstream file(filename, std::ios::binary);
//   assert(file && "Failed to open wav file");
//
//   RiffHeader riff{};
//   file.read(reinterpret_cast<char *>(&riff), sizeof(riff));
//   assert(file && "Failed to read RIFF header");
//   assert(FourCCEquals(riff.chunkId, "RIFF") && "Not RIFF");
//   assert(FourCCEquals(riff.format, "WAVE") && "Not WAVE");
//
//   SoundManager::SoundData sd{};
//
//   bool foundFmt = false;
//   bool foundData = false;
//
//   while (file && (!foundFmt || !foundData)) {
//     ChunkHeader ch{};
//     file.read(reinterpret_cast<char *>(&ch), sizeof(ch));
//     if (!file) {
//       break;
//     }
//
//     if (FourCCEquals(ch.id, "fmt ")) {
//       // fmt chunk（WAVEFORMATEXより大きい場合があるので先頭だけ読む）
//       const uint32_t readSize =
//           (ch.size < sizeof(WAVEFORMATEX))
//               ? ch.size
//               : static_cast<uint32_t>(sizeof(WAVEFORMATEX));
//
//       file.read(reinterpret_cast<char *>(&sd.wfex), readSize);
//       assert(file && "Failed to read fmt chunk");
//
//       if (ch.size > readSize) {
//         file.seekg(static_cast<std::streamoff>(ch.size - readSize),
//                    std::ios::cur);
//       }
//
//       const uint32_t pad = Align2(ch.size) - ch.size;
//       if (pad) {
//         file.seekg(pad, std::ios::cur);
//       }
//
//       foundFmt = true;
//
//     } else if (FourCCEquals(ch.id, "data")) {
//       sd.bufferSize = ch.size;
//       sd.pBuffer = new BYTE[sd.bufferSize];
//       file.read(reinterpret_cast<char *>(sd.pBuffer), sd.bufferSize);
//       assert(file && "Failed to read data chunk");
//
//       const uint32_t pad = Align2(ch.size) - ch.size;
//       if (pad) {
//         file.seekg(pad, std::ios::cur);
//       }
//
//       foundData = true;
//
//     } else {
//       // その他チャンクは飛ばす（JUNK/LIST/fact等）
//       file.seekg(static_cast<std::streamoff>(Align2(ch.size)),
//       std::ios::cur);
//     }
//   }
//
//   assert(foundFmt && "fmt chunk not found");
//   assert(foundData && "data chunk not found");
//
//   return sd;
// }

// FMTチャンク
// struct FormatChunk {
//  ChunkHeader chunk; //"fmt"
//  WAVEFORMATEX fmt;  // 波形フォーマット
//};
} // namespace

SoundManager *SoundManager::GetInstance() {
  static SoundManager instance;
  return &instance;
}

void SoundManager::Initialize(IXAudio2 *xAudio2) {

  HRESULT result;

  result = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);

  assert(SUCCEEDED(result));

  assert(xAudio2);
  xAudio2_ = xAudio2;
}

void SoundManager::Finalize() {
  // 再生中Voiceをまとめて破棄（簡易）
  // for (IXAudio2SourceVoice *v : activeVoices_) {
  //  if (!v) {
  //    continue;
  //  }
  //  v->Stop();
  //  v->FlushSourceBuffers();
  //  v->DestroyVoice();
  //}
  // activeVoices_.clear();

  // xAudio2_ = nullptr;

  StopBGM();

  // SE voice 破棄
  for (IXAudio2SourceVoice *v : seVoices_) {
    if (!v) {
      continue;
    }
    v->Stop();
    v->FlushSourceBuffers();
    v->DestroyVoice();
  }
  seVoices_.clear();
  seVoiceKeys_.clear();

  // WAV解放
  // for (auto &[key, sd] : sounds_) {
  //  delete[] sd.pBuffer;
  //  sd.pBuffer = nullptr;
  //  sd.bufferSize = 0;
  //  sd.wfex = {};
  //}

  assert(!bgmVoice_ && "Finalize(): BGM voice still alive");
  assert(seVoices_.empty() && "Finalize(): SE voices still alive");

  sounds_.clear();

  xAudio2_ = nullptr;

  HRESULT result;

  result = MFShutdown();
  assert(SUCCEEDED(result));
}

void SoundManager::Update() {
  // 再生が終わったSE voiceを回収
  auto it = seVoices_.begin();
  while (it != seVoices_.end()) {
    IXAudio2SourceVoice *v = *it;
    if (!v) {
      it = seVoices_.erase(it);
      continue;
    }

    XAUDIO2_VOICE_STATE state{};
    v->GetState(&state);

    if (state.BuffersQueued == 0) {
      v->Stop();
      v->FlushSourceBuffers();
      v->DestroyVoice();

      seVoiceKeys_.erase(v);

      it = seVoices_.erase(it);
    } else {
      ++it;
    }
  }
}

SoundManager::SoundData
SoundManager::SoundLoadFile(const std::string &filename) {
  // 1) SourceReader作成
  const std::wstring wpath = ToWide(filename);

  ComPtr<IMFSourceReader> reader;
  HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
  assert(SUCCEEDED(hr));

  // 2) 出力をPCMに固定
  ComPtr<IMFMediaType> pcmType;
  hr = MFCreateMediaType(&pcmType);
  assert(SUCCEEDED(hr));

  hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  assert(SUCCEEDED(hr));
  hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  assert(SUCCEEDED(hr));

  hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr,
                                   pcmType.Get());
  assert(SUCCEEDED(hr));

  // 3) 確定したメディアタイプ → WAVEFORMATEXへ
  ComPtr<IMFMediaType> outType;
  hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                   &outType);
  assert(SUCCEEDED(hr));

  WAVEFORMATEX *wf = nullptr;
  UINT32 wfSize = 0;
  hr = MFCreateWaveFormatExFromMFMediaType(outType.Get(), &wf, &wfSize);
  assert(SUCCEEDED(hr));
  assert(wf);

  SoundData sd{};
  sd.waveFormat.resize(wfSize);
  std::memcpy(sd.waveFormat.data(), wf, wfSize);

  CoTaskMemFree(wf);
  wf = nullptr;

  // reserve（再生時間から概算）
  {
    PROPVARIANT var{};
    PropVariantInit(&var);

    hr = reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                          MF_PD_DURATION, &var);

    if (SUCCEEDED(hr) && var.vt == VT_UI8) {
      const ULONGLONG duration100ns = var.uhVal.QuadPart; // 100ns単位
      const double seconds = static_cast<double>(duration100ns) / 10000000.0;

      const WAVEFORMATEX *wfex = sd.GetWfex();
      if (wfex && wfex->nAvgBytesPerSec > 0) {
        const size_t estimated = static_cast<size_t>(
            seconds * static_cast<double>(wfex->nAvgBytesPerSec));

        // ちょい多めに確保（再確保防止）
        sd.buffer.reserve(estimated + (estimated / 8));
      }
    }

    PropVariantClear(&var);
  }

  // 4) PCMデータを全部読む
  while (true) {
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;

    hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                            &streamIndex, &flags, &timestamp, &sample);
    assert(SUCCEEDED(hr));

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
      break;
    }
    if (!sample) {
      continue;
    }

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    assert(SUCCEEDED(hr));

    BYTE *data = nullptr;
    DWORD maxLen = 0;
    DWORD curLen = 0;
    hr = buffer->Lock(&data, &maxLen, &curLen);
    assert(SUCCEEDED(hr));

    if (curLen > 0) {
      const size_t old = sd.buffer.size();
      sd.buffer.resize(old + curLen);
      std::memcpy(sd.buffer.data() + old, data, curLen);
    }

    buffer->Unlock();
  }

  assert(!sd.buffer.empty());
  return sd;
}

void SoundManager::Load(const std::string &key, const std::string &filename) {
  assert(xAudio2_ && "SoundManager::Initialize must be called before Load().");

  if (sounds_.find(key) != sounds_.end()) {
    return;
  }

  SoundData sd = SoundLoadFile(filename);
  sounds_.emplace(key, std::move(sd));
}

void SoundManager::Unload(const std::string &key) {
  auto it = sounds_.find(key);
  if (it == sounds_.end()) {
    return;
  }

  // 再生中のSEのUnload禁止
  assert(seVoices_.empty() &&
         "Unload() while SE is playing is dangerous. StopAllSE() first.");

  if (bgmVoice_ && bgmKey_ == key) {
    StopBGM();
  }

  // SoundData &sd = it->second;
  // delete[] sd.pBuffer;
  // sd.pBuffer = nullptr;
  // sd.bufferSize = 0;
  // sd.wfex = {};

  sounds_.erase(it);
}

void SoundManager::PlaySE(const std::string &key, float volume, float pitchRatio) {
  assert(xAudio2_ &&
         "SoundManager::Initialize must be called before PlaySE().");

  auto it = sounds_.find(key);
  assert(it != sounds_.end() && "Sound key not found. Call Load() first.");

  const SoundData &sd = it->second;
  assert(!sd.buffer.empty());

  IXAudio2SourceVoice *voice = nullptr;
  HRESULT hr = xAudio2_->CreateSourceVoice(&voice, sd.GetWfex());
  assert(SUCCEEDED(hr));

  if (volume != 1.0f) {
    voice->SetVolume(volume);
  }
  if (pitchRatio != 1.0f) {
    voice->SetFrequencyRatio(pitchRatio);
  }

  XAUDIO2_BUFFER buf{};
  buf.pAudioData = sd.buffer.data();
  buf.AudioBytes = static_cast<UINT32>(sd.buffer.size());
  buf.Flags = XAUDIO2_END_OF_STREAM;

  hr = voice->SubmitSourceBuffer(&buf);
  assert(SUCCEEDED(hr));
  hr = voice->Start();
  assert(SUCCEEDED(hr));

  // 再生終了後Updateで回収
  seVoices_.push_back(voice);
  seVoiceKeys_[voice] = key;
}

void SoundManager::PlaySE_Once(const std::string &key) {

  for (IXAudio2SourceVoice *v : seVoices_) {
    if (!v) {
      continue;
    }

    auto it = seVoiceKeys_.find(v);
    if (it == seVoiceKeys_.end()) {
      continue;
    }

    // 同じkeyのvoiceだけ見る
    if (it->second != key) {
      continue;
    }

    XAUDIO2_VOICE_STATE st{};
    v->GetState(&st);

    // まだ鳴ってるなら無視
    if (st.BuffersQueued > 0) {
      return;
    }
  }

  PlaySE(key);
}

void SoundManager::PlayBGM(const std::string &key) {
  assert(xAudio2_ &&
         "SoundManager::Initialize must be called before PlayBGM().");

  auto it = sounds_.find(key);
  assert(it != sounds_.end() && "Sound key not found. Call Load() first.");

  if (bgmVoice_ && bgmKey_ == key) {
    return; // 既に同じBGMが鳴っている
  }

  StopBGM();

  const SoundData &sd = it->second;
  assert(!sd.buffer.empty());

  HRESULT hr = xAudio2_->CreateSourceVoice(&bgmVoice_, sd.GetWfex());
  assert(SUCCEEDED(hr));

  XAUDIO2_BUFFER buf{};
  buf.pAudioData = sd.buffer.data();
  buf.AudioBytes = static_cast<UINT32>(sd.buffer.size());
  buf.Flags = XAUDIO2_END_OF_STREAM;
  buf.LoopCount = XAUDIO2_LOOP_INFINITE;

  hr = bgmVoice_->SubmitSourceBuffer(&buf);
  assert(SUCCEEDED(hr));
  hr = bgmVoice_->Start();
  assert(SUCCEEDED(hr));

  bgmKey_ = key;
}

void SoundManager::StopBGM() {
  if (!bgmVoice_) {
    bgmKey_.clear();
    return;
  }
  bgmVoice_->Stop();
  bgmVoice_->FlushSourceBuffers();
  bgmVoice_->DestroyVoice();
  bgmVoice_ = nullptr;
  bgmKey_.clear();
}

bool SoundManager::Exists(const std::string &key) const {
  return sounds_.find(key) != sounds_.end();
}

// SoundManager::SoundData
// SoundManager::SoundLoadFile(const std::string &filename) {
//
//   // フルパスをワイド文字列に変換
//   const std::string fullpath = filename;
//   std::wstring filePathW(fullpath.begin(), fullpath.end());
//
//   HRESULT result;
//
//   // SourceReader作成
//   ComPtr<IMFSourceReader> pReader;
//   result = MFCreateSourceReaderFromURL(filePathW.c_str(), nullptr, &pReader);
//   assert(SUCCEEDED(result));
//
//   // PCM形式にフォーマット指定
//   Microsoft::WRL::ComPtr<IMFMediaType> pPCMType;
//   HRESULT result = MFCreateMediaType(&pPCMType);
//   assert(SUCCEEDED(result));
//
//   result = pPCMType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
//   assert(SUCCEEDED(result));
//
//   result = pPCMType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
//   assert(SUCCEEDED(result));
//
//   result = pReader->SetCurrentMediaType(
//       (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPCMType.Get());
//   assert(SUCCEEDED(result));
//
//   // 実際にセットされたメディアタイプを取得
//   Microsoft::WRL::ComPtr<IMFMediaType> pOutType;
//   HRESULT result = pReader->GetCurrentMediaType(
//       (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pOutType);
//   assert(SUCCEEDED(result));
//
//   // Waveフォーマットを取得
//   WAVEFORMATEX *waveFormat = nullptr;
//   result =
//       MFCreateWaveFormatExFromMFMediaType(pOutType.Get(), &waveFormat,
//       nullptr);
//   assert(SUCCEEDED(result));
//
//   // 音声データ格納用
//   SoundData soundData{};
//   soundData.wfex = *waveFormat;
//
//   // Waveフォーマット用メモリ解放
//   CoTaskMemFree(waveFormat);
//   waveFormat = nullptr;
//
//   // PCM波形データ取得
//   while (true) {
//     ComPtr<IMFSample> pSample;
//     DWORD streamIndex = 0;
//     DWORD flags = 0;
//     LONGLONG llTimeStamp = 0;
//
//     result = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
//                                  &streamIndex, &flags, &llTimeStamp,
//                                  &pSample);
//
//     assert(SUCCEEDED(result));
//
//     // ストリーム終端
//     if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
//       break;
//     }
//
//     if (!pSample) {
//       continue;
//     }
//
//     // サンプルからバッファ取得
//     ComPtr<IMFMediaBuffer> pBuffer;
//     result = pSample->ConvertToContiguousBuffer(&pBuffer);
//     assert(SUCCEEDED(result));
//
//     BYTE *pData = nullptr;
//     DWORD maxLength = 0;
//     DWORD currentLength = 0;
//
//     result = pBuffer->Lock(&pData, &maxLength, &currentLength);
//     assert(SUCCEEDED(result));
//
//     // PCMデータを追加
//     soundData.buffer.insert(soundData.buffer.end(), pData,
//                             pData + currentLength);
//
//     pBuffer->Unlock();
//   }
//
//   // wav
//   return LoadWavInternal(filename.c_str());
//
//   ///* ファイルオープン
//   //----------------------*/
//
//   //// ファイル入力ストリームのインスタンス
//   // std::ifstream file;
//
//   ////.wavファイルをバイナリモードで開く
//   // file.open(filename, std::ios_base::binary);
//
//   //// ファイルが開けなかったら
//   // assert(file.is_open());
//
//   ///* .wavデータ読み込み
//   //----------------------*/
//
//   //// RIFFヘッダーの読み込み
//   // RiffHeader riff;
//
//   // file.read((char *)&riff, sizeof(riff));
//
//   //// 開いたファイルがRIFFであるかを確認する
//   // if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
//   //   assert(0);
//   // }
//
//   //// タイプがWAVEか確認
//   // if (strncmp(riff.type, "WAVE", 4) != 0) {
//   //   assert(0);
//   // }
//
//   //// formatチャンクの読み込み
//   // FormatChunk format = {};
//
//   //// チャンクヘッダーの確認
//   // file.read((char *)&format, sizeof(ChunkHeader));
//
//   // if (strncmp(format.chunk.id, "fmt ", 4) != 0) {
//   //   assert(0);
//   // }
//
//   //// チャンク本体の読み込み
//   // assert(format.chunk.size <= sizeof(format.fmt));
//   // file.read((char *)&format.fmt, format.chunk.size);
//
//   //// Dataチャンクの読み込み
//   // ChunkHeader data;
//
//   // file.read((char *)&data, sizeof(data));
//
//   //// Junkチャンク(パディング？)を検出した場合
//   // if (strncmp(data.id, "JUNK", 4) == 0) {
//
//   //  // 読み取り位置をJunkチャンクの終わりまで進める
//   //  file.seekg(data.size, std::ios_base::cur);
//
//   //  // 飛ばした後に再度読み込み
//   //  file.read((char *)&data, sizeof(data));
//   //}
//
//   // if (strncmp(data.id, "data", 4) != 0) {
//   //   assert(0);
//   // }
//
//   //// Dataチャンクのデータ部(波形データ)の読み込み
//   // char *pBuffer = new char[data.size];
//   // file.read(pBuffer, data.size);
//
//   //// Waveファイルを閉じる
//   // file.close();
//
//   ///* 読み込んだ音声データをreturn
//   //-------------------------------*/
//
//   //// returnするための音声データ
//   // SoundData soundData = {};
//
//   // soundData.wfex = format.fmt;
//   // soundData.pBuffer = reinterpret_cast<BYTE *>(pBuffer);
//   // soundData.bufferSize = data.size;
//
//   // return soundData;
// }

// void SoundManager::SoundUnload(SoundData *soundData) {
//   if (!soundData) {
//     return;
//   }
//
//   soundData->buffer.clear();
//   soundData->wfex = {};
// }
//
// void SoundManager::SoundPlayWave(IXAudio2 *xAudio2,
//                                 const SoundData &soundData) {
//  assert(xAudio2);
//  assert(soundData.pBuffer && soundData.bufferSize > 0);
//
//  IXAudio2SourceVoice *voice = nullptr;
//  HRESULT hr = xAudio2->CreateSourceVoice(&voice, &soundData.wfex);
//  assert(SUCCEEDED(hr));
//
//  XAUDIO2_BUFFER buf{};
//  buf.pAudioData = soundData.pBuffer;
//  buf.AudioBytes = soundData.bufferSize;
//  buf.Flags = XAUDIO2_END_OF_STREAM;
//
//  hr = voice->SubmitSourceBuffer(&buf);
//  assert(SUCCEEDED(hr));
//  hr = voice->Start();
//  assert(SUCCEEDED(hr));
//
//  seVoices_.push_back(voice);
//
//  // HRESULT result;
//
//  //// 波形フォーマットを元にSourceVoiceの生成
//  // IXAudio2SourceVoice *pSourceVoice = nullptr;
//  // result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
//  // assert(SUCCEEDED(result));
//
//  //// 再生する波形データの設定
//  // XAUDIO2_BUFFER buf{};
//  // buf.pAudioData = soundData.pBuffer;
//  // buf.AudioBytes = soundData.bufferSize;
//  // buf.Flags = XAUDIO2_END_OF_STREAM;
//
//  //// 波形データの再生
//  // result = pSourceVoice->SubmitSourceBuffer(&buf);
//  // result = pSourceVoice->Start();
//
//  // activeVoices_.push_back(pSourceVoice);
//}
//
// void SoundManager::SoundPlayWave(const SoundData &soundData) {
//  assert(xAudio2_ &&
//         "SoundManager::Initialize must be called before SoundPlayWave().");
//  SoundPlayWave(xAudio2_, soundData);
//}

void SoundManager::StopAllSE() {
  // いま鳴っているSEをすべて停止し、voiceも破棄する
  for (IXAudio2SourceVoice *v : seVoices_) {
    if (!v) {
      continue;
    }
    v->Stop();
    v->FlushSourceBuffers();
    v->DestroyVoice();
  }
  seVoices_.clear();
  seVoiceKeys_.clear();
}
