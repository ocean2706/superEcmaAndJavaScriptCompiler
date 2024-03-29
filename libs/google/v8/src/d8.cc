// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// Defined when linking against shared lib on Windows.
#if defined(USING_V8_SHARED) && !defined(V8_SHARED)
#define V8_SHARED
#endif

#ifdef COMPRESS_STARTUP_DATA_BZ2
#include <bzlib.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// TODO(dcarney): remove
#define V8_ALLOW_ACCESS_TO_PERSISTENT_ARROW
#define V8_ALLOW_ACCESS_TO_RAW_HANDLE_CONSTRUCTOR
#define V8_ALLOW_ACCESS_TO_PERSISTENT_IMPLICIT

#ifdef V8_SHARED
#include <assert.h>
#endif  // V8_SHARED

#ifndef V8_SHARED
#include <algorithm>
#endif  // !V8_SHARED

#ifdef V8_SHARED
#include "../include/v8-testing.h"
#endif  // V8_SHARED

#ifdef ENABLE_VTUNE_JIT_INTERFACE
#include "third_party/vtune/v8-vtune.h"
#endif

#include "d8.h"

#ifndef V8_SHARED
#include "api.h"
#include "checks.h"
#include "d8-debug.h"
#include "debug.h"
#include "natives.h"
#include "platform.h"
#include "v8.h"
#endif  // V8_SHARED

#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>  // NOLINT
#endif

#ifndef ASSERT
#define ASSERT(condition) assert(condition)
#endif

namespace v8 {


static Handle<Value> Throw(const char* message) {
  return ThrowException(String::New(message));
}


// TODO(rossberg): should replace these by proper uses of HasInstance,
// once we figure out a good way to make the templates global.
const char kArrayBufferMarkerPropName[] = "d8::_is_array_buffer_";
const char kArrayMarkerPropName[] = "d8::_is_typed_array_";


#define FOR_EACH_STRING(V) \
  V(ArrayBuffer, "ArrayBuffer") \
  V(ArrayBufferMarkerPropName, kArrayBufferMarkerPropName) \
  V(ArrayMarkerPropName, kArrayMarkerPropName) \
  V(buffer, "buffer") \
  V(byteLength, "byteLength") \
  V(byteOffset, "byteOffset") \
  V(BYTES_PER_ELEMENT, "BYTES_PER_ELEMENT")  \
  V(length, "length")


class PerIsolateData {
 public:
  explicit PerIsolateData(Isolate* isolate) : isolate_(isolate), realms_(NULL) {
    HandleScope scope(isolate);
#define INIT_STRING(name, value) \
    name##_string_ = Persistent<String>::New(isolate, String::NewSymbol(value));
    FOR_EACH_STRING(INIT_STRING)
#undef INIT_STRING
    isolate->SetData(this);
  }

  ~PerIsolateData() {
#define DISPOSE_STRING(name, value) name##_string_.Dispose(isolate_);
    FOR_EACH_STRING(DISPOSE_STRING)
#undef DISPOSE_STRING
    isolate_->SetData(NULL);  // Not really needed, just to be sure...
  }

  inline static PerIsolateData* Get(Isolate* isolate) {
    return reinterpret_cast<PerIsolateData*>(isolate->GetData());
  }

#define DEFINE_STRING_GETTER(name, value) \
  static Handle<String> name##_string(Isolate* isolate) { \
    return Handle<String>(*Get(isolate)->name##_string_); \
  }
  FOR_EACH_STRING(DEFINE_STRING_GETTER)
#undef DEFINE_STRING_GETTER

  class RealmScope {
   public:
    explicit RealmScope(PerIsolateData* data);
    ~RealmScope();
   private:
    PerIsolateData* data_;
  };

 private:
  friend class Shell;
  friend class RealmScope;
  Isolate* isolate_;
  int realm_count_;
  int realm_current_;
  int realm_switch_;
  Persistent<Context>* realms_;
  Persistent<Value> realm_shared_;

#define DEFINE_MEMBER(name, value) Persistent<String> name##_string_;
  FOR_EACH_STRING(DEFINE_MEMBER)
#undef DEFINE_MEMBER

  int RealmFind(Handle<Context> context);
};


LineEditor *LineEditor::current_ = NULL;


LineEditor::LineEditor(Type type, const char* name)
    : type_(type), name_(name) {
  if (current_ == NULL || current_->type_ < type) current_ = this;
}


class DumbLineEditor: public LineEditor {
 public:
  explicit DumbLineEditor(Isolate* isolate)
      : LineEditor(LineEditor::DUMB, "dumb"), isolate_(isolate) { }
  virtual Handle<String> Prompt(const char* prompt);
 private:
  Isolate* isolate_;
};


Handle<String> DumbLineEditor::Prompt(const char* prompt) {
  printf("%s", prompt);
#if defined(__native_client__)
  // Native Client libc is used to being embedded in Chrome and
  // has trouble recognizing when to flush.
  fflush(stdout);
#endif
  return Shell::ReadFromStdin(isolate_);
}


#ifndef V8_SHARED
CounterMap* Shell::counter_map_;
i::OS::MemoryMappedFile* Shell::counters_file_ = NULL;
CounterCollection Shell::local_counters_;
CounterCollection* Shell::counters_ = &local_counters_;
i::Mutex* Shell::context_mutex_(i::OS::CreateMutex());
Persistent<Context> Shell::utility_context_;
#endif  // V8_SHARED

Persistent<Context> Shell::evaluation_context_;
ShellOptions Shell::options;
const char* Shell::kPrompt = "d8> ";


const int MB = 1024 * 1024;


#ifndef V8_SHARED
bool CounterMap::Match(void* key1, void* key2) {
  const char* name1 = reinterpret_cast<const char*>(key1);
  const char* name2 = reinterpret_cast<const char*>(key2);
  return strcmp(name1, name2) == 0;
}
#endif  // V8_SHARED


// Converts a V8 value to a C string.
const char* Shell::ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}


// Executes a string within the current v8 context.
bool Shell::ExecuteString(Isolate* isolate,
                          Handle<String> source,
                          Handle<Value> name,
                          bool print_result,
                          bool report_exceptions) {
#if !defined(V8_SHARED) && defined(ENABLE_DEBUGGER_SUPPORT)
  bool FLAG_debugger = i::FLAG_debugger;
#else
  bool FLAG_debugger = false;
#endif  // !V8_SHARED && ENABLE_DEBUGGER_SUPPORT
  HandleScope handle_scope(isolate);
  TryCatch try_catch;
  options.script_executed = true;
  if (FLAG_debugger) {
    // When debugging make exceptions appear to be uncaught.
    try_catch.SetVerbose(true);
  }
  Handle<Script> script = Script::New(source, name);
  if (script.IsEmpty()) {
    // Print errors that happened during compilation.
    if (report_exceptions && !FLAG_debugger)
      ReportException(isolate, &try_catch);
    return false;
  } else {
    PerIsolateData* data = PerIsolateData::Get(isolate);
    Local<Context> realm =
        Local<Context>::New(isolate, data->realms_[data->realm_current_]);
    realm->Enter();
    Handle<Value> result = script->Run();
    realm->Exit();
    data->realm_current_ = data->realm_switch_;
    if (result.IsEmpty()) {
      ASSERT(try_catch.HasCaught());
      // Print errors that happened during execution.
      if (report_exceptions && !FLAG_debugger)
        ReportException(isolate, &try_catch);
      return false;
    } else {
      ASSERT(!try_catch.HasCaught());
      if (print_result) {
#if !defined(V8_SHARED)
        if (options.test_shell) {
#endif
          if (!result->IsUndefined()) {
            // If all went well and the result wasn't undefined then print
            // the returned value.
            v8::String::Utf8Value str(result);
            fwrite(*str, sizeof(**str), str.length(), stdout);
            printf("\n");
          }
#if !defined(V8_SHARED)
        } else {
          v8::TryCatch try_catch;
          Context::Scope context_scope(isolate, utility_context_);
          Handle<Object> global = utility_context_->Global();
          Handle<Value> fun = global->Get(String::New("Stringify"));
          Handle<Value> argv[1] = { result };
          Handle<Value> s = Handle<Function>::Cast(fun)->Call(global, 1, argv);
          if (try_catch.HasCaught()) return true;
          v8::String::Utf8Value str(s);
          fwrite(*str, sizeof(**str), str.length(), stdout);
          printf("\n");
        }
#endif
      }
      return true;
    }
  }
}


PerIsolateData::RealmScope::RealmScope(PerIsolateData* data) : data_(data) {
  data_->realm_count_ = 1;
  data_->realm_current_ = 0;
  data_->realm_switch_ = 0;
  data_->realms_ = new Persistent<Context>[1];
  data_->realms_[0] =
      Persistent<Context>::New(data_->isolate_, Context::GetEntered());
  data_->realm_shared_.Clear();
}


PerIsolateData::RealmScope::~RealmScope() {
  // Drop realms to avoid keeping them alive.
  for (int i = 0; i < data_->realm_count_; ++i)
    data_->realms_[i].Dispose(data_->isolate_);
  delete[] data_->realms_;
  if (!data_->realm_shared_.IsEmpty())
    data_->realm_shared_.Dispose(data_->isolate_);
}


int PerIsolateData::RealmFind(Handle<Context> context) {
  for (int i = 0; i < realm_count_; ++i) {
    if (realms_[i] == context) return i;
  }
  return -1;
}


// Realm.current() returns the index of the currently active realm.
Handle<Value> Shell::RealmCurrent(const Arguments& args) {
  Isolate* isolate = args.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  int index = data->RealmFind(Context::GetEntered());
  if (index == -1) return Undefined(isolate);
  return Number::New(index);
}


// Realm.owner(o) returns the index of the realm that created o.
Handle<Value> Shell::RealmOwner(const Arguments& args) {
  Isolate* isolate = args.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  if (args.Length() < 1 || !args[0]->IsObject()) {
    return Throw("Invalid argument");
  }
  int index = data->RealmFind(args[0]->ToObject()->CreationContext());
  if (index == -1) return Undefined(isolate);
  return Number::New(index);
}


// Realm.global(i) returns the global object of realm i.
// (Note that properties of global objects cannot be read/written cross-realm.)
Handle<Value> Shell::RealmGlobal(const Arguments& args) {
  PerIsolateData* data = PerIsolateData::Get(args.GetIsolate());
  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return Throw("Invalid argument");
  }
  int index = args[0]->Uint32Value();
  if (index >= data->realm_count_ || data->realms_[index].IsEmpty()) {
    return Throw("Invalid realm index");
  }
  return data->realms_[index]->Global();
}


// Realm.create() creates a new realm and returns its index.
Handle<Value> Shell::RealmCreate(const Arguments& args) {
  Isolate* isolate = args.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  Persistent<Context>* old_realms = data->realms_;
  int index = data->realm_count_;
  data->realms_ = new Persistent<Context>[++data->realm_count_];
  for (int i = 0; i < index; ++i) data->realms_[i] = old_realms[i];
  delete[] old_realms;
  Handle<ObjectTemplate> global_template = CreateGlobalTemplate(isolate);
  data->realms_[index] = Persistent<Context>::New(
      isolate, Context::New(isolate, NULL, global_template));
  return Number::New(index);
}


// Realm.dispose(i) disposes the reference to the realm i.
Handle<Value> Shell::RealmDispose(const Arguments& args) {
  Isolate* isolate = args.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return Throw("Invalid argument");
  }
  int index = args[0]->Uint32Value();
  if (index >= data->realm_count_ || data->realms_[index].IsEmpty() ||
      index == 0 ||
      index == data->realm_current_ || index == data->realm_switch_) {
    return Throw("Invalid realm index");
  }
  data->realms_[index].Dispose(isolate);
  data->realms_[index].Clear();
  return Undefined(isolate);
}


// Realm.switch(i) switches to the realm i for consecutive interactive inputs.
Handle<Value> Shell::RealmSwitch(const Arguments& args) {
  Isolate* isolate = args.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  if (args.Length() < 1 || !args[0]->IsNumber()) {
    return Throw("Invalid argument");
  }
  int index = args[0]->Uint32Value();
  if (index >= data->realm_count_ || data->realms_[index].IsEmpty()) {
    return Throw("Invalid realm index");
  }
  data->realm_switch_ = index;
  return Undefined(isolate);
}


// Realm.eval(i, s) evaluates s in realm i and returns the result.
Handle<Value> Shell::RealmEval(const Arguments& args) {
  Isolate* isolate = args.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsString()) {
    return Throw("Invalid argument");
  }
  int index = args[0]->Uint32Value();
  if (index >= data->realm_count_ || data->realms_[index].IsEmpty()) {
    return Throw("Invalid realm index");
  }
  Handle<Script> script = Script::New(args[1]->ToString());
  if (script.IsEmpty()) return Undefined(isolate);
  Local<Context> realm = Local<Context>::New(isolate, data->realms_[index]);
  realm->Enter();
  Handle<Value> result = script->Run();
  realm->Exit();
  return result;
}


// Realm.shared is an accessor for a single shared value across realms.
Handle<Value> Shell::RealmSharedGet(Local<String> property,
                                    const AccessorInfo& info) {
  Isolate* isolate = info.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  if (data->realm_shared_.IsEmpty()) return Undefined(isolate);
  return Local<Value>::New(isolate, data->realm_shared_);
}

void Shell::RealmSharedSet(Local<String> property,
                           Local<Value> value,
                           const AccessorInfo& info) {
  Isolate* isolate = info.GetIsolate();
  PerIsolateData* data = PerIsolateData::Get(isolate);
  if (!data->realm_shared_.IsEmpty()) data->realm_shared_.Dispose(isolate);
  data->realm_shared_ = Persistent<Value>::New(isolate, value);
}


Handle<Value> Shell::Print(const Arguments& args) {
  Handle<Value> val = Write(args);
  printf("\n");
  fflush(stdout);
  return val;
}


Handle<Value> Shell::Write(const Arguments& args) {
  for (int i = 0; i < args.Length(); i++) {
    HandleScope handle_scope(args.GetIsolate());
    if (i != 0) {
      printf(" ");
    }

    // Explicitly catch potential exceptions in toString().
    v8::TryCatch try_catch;
    Handle<String> str_obj = args[i]->ToString();
    if (try_catch.HasCaught()) return try_catch.ReThrow();

    v8::String::Utf8Value str(str_obj);
    int n = static_cast<int>(fwrite(*str, sizeof(**str), str.length(), stdout));
    if (n != str.length()) {
      printf("Error in fwrite\n");
      Exit(1);
    }
  }
  return Undefined(args.GetIsolate());
}


Handle<Value> Shell::EnableProfiler(const Arguments& args) {
  V8::ResumeProfiler();
  return Undefined(args.GetIsolate());
}


Handle<Value> Shell::DisableProfiler(const Arguments& args) {
  V8::PauseProfiler();
  return Undefined(args.GetIsolate());
}


Handle<Value> Shell::Read(const Arguments& args) {
  String::Utf8Value file(args[0]);
  if (*file == NULL) {
    return Throw("Error loading file");
  }
  Handle<String> source = ReadFile(args.GetIsolate(), *file);
  if (source.IsEmpty()) {
    return Throw("Error loading file");
  }
  return source;
}


Handle<String> Shell::ReadFromStdin(Isolate* isolate) {
  static const int kBufferSize = 256;
  char buffer[kBufferSize];
  Handle<String> accumulator = String::New("");
  int length;
  while (true) {
    // Continue reading if the line ends with an escape '\\' or the line has
    // not been fully read into the buffer yet (does not end with '\n').
    // If fgets gets an error, just give up.
    char* input = NULL;
    {  // Release lock for blocking input.
      Unlocker unlock(isolate);
      input = fgets(buffer, kBufferSize, stdin);
    }
    if (input == NULL) return Handle<String>();
    length = static_cast<int>(strlen(buffer));
    if (length == 0) {
      return accumulator;
    } else if (buffer[length-1] != '\n') {
      accumulator = String::Concat(accumulator, String::New(buffer, length));
    } else if (length > 1 && buffer[length-2] == '\\') {
      buffer[length-2] = '\n';
      accumulator = String::Concat(accumulator, String::New(buffer, length-1));
    } else {
      return String::Concat(accumulator, String::New(buffer, length-1));
    }
  }
}


Handle<Value> Shell::Load(const Arguments& args) {
  for (int i = 0; i < args.Length(); i++) {
    HandleScope handle_scope(args.GetIsolate());
    String::Utf8Value file(args[i]);
    if (*file == NULL) {
      return Throw("Error loading file");
    }
    Handle<String> source = ReadFile(args.GetIsolate(), *file);
    if (source.IsEmpty()) {
      return Throw("Error loading file");
    }
    if (!ExecuteString(args.GetIsolate(),
                       source,
                       String::New(*file),
                       false,
                       true)) {
      return Throw("Error executing file");
    }
  }
  return Undefined(args.GetIsolate());
}

static int32_t convertToInt(Local<Value> value_in, TryCatch* try_catch) {
  if (value_in->IsInt32()) {
    return value_in->Int32Value();
  }

  Local<Value> number = value_in->ToNumber();
  if (try_catch->HasCaught()) return 0;

  ASSERT(number->IsNumber());
  Local<Int32> int32 = number->ToInt32();
  if (try_catch->HasCaught() || int32.IsEmpty()) return 0;

  int32_t value = int32->Int32Value();
  if (try_catch->HasCaught()) return 0;

  return value;
}


static int32_t convertToUint(Local<Value> value_in, TryCatch* try_catch) {
  int32_t raw_value = convertToInt(value_in, try_catch);
  if (try_catch->HasCaught()) return 0;

  if (raw_value < 0) {
    Throw("Array length must not be negative.");
    return 0;
  }

  static const int kMaxLength = 0x3fffffff;
#ifndef V8_SHARED
  ASSERT(kMaxLength == i::ExternalArray::kMaxLength);
#endif  // V8_SHARED
  if (raw_value > static_cast<int32_t>(kMaxLength)) {
    Throw("Array length exceeds maximum length.");
  }
  return raw_value;
}


Handle<Value> Shell::CreateExternalArrayBuffer(Isolate* isolate,
                                               Handle<Object> buffer,
                                               int32_t length) {
  static const int32_t kMaxSize = 0x7fffffff;
  // Make sure the total size fits into a (signed) int.
  if (length < 0 || length > kMaxSize) {
    return Throw("ArrayBuffer exceeds maximum size (2G)");
  }
  uint8_t* data = new uint8_t[length];
  if (data == NULL) {
    return Throw("Memory allocation failed");
  }
  memset(data, 0, length);

  buffer->SetHiddenValue(
      PerIsolateData::ArrayBufferMarkerPropName_string(isolate), True());
  Persistent<Object> persistent_array =
      Persistent<Object>::New(isolate, buffer);
  persistent_array.MakeWeak(isolate, data, ExternalArrayWeakCallback);
  persistent_array.MarkIndependent(isolate);
  isolate->AdjustAmountOfExternalAllocatedMemory(length);

  buffer->SetIndexedPropertiesToExternalArrayData(
      data, v8::kExternalByteArray, length);
  buffer->Set(PerIsolateData::byteLength_string(isolate),
              Int32::New(length, isolate),
              ReadOnly);

  return buffer;
}


Handle<Value> Shell::ArrayBuffer(const Arguments& args) {
  if (!args.IsConstructCall()) {
    Handle<Value>* rec_args = new Handle<Value>[args.Length()];
    for (int i = 0; i < args.Length(); ++i) rec_args[i] = args[i];
    Handle<Value> result = args.Callee()->NewInstance(args.Length(), rec_args);
    delete[] rec_args;
    return result;
  }

  if (args.Length() == 0) {
    return Throw("ArrayBuffer constructor must have one argument");
  }
  TryCatch try_catch;
  int32_t length = convertToUint(args[0], &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();

  return CreateExternalArrayBuffer(args.GetIsolate(), args.This(), length);
}


Handle<Object> Shell::CreateExternalArray(Isolate* isolate,
                                          Handle<Object> array,
                                          Handle<Object> buffer,
                                          ExternalArrayType type,
                                          int32_t length,
                                          int32_t byteLength,
                                          int32_t byteOffset,
                                          int32_t element_size) {
  ASSERT(element_size == 1 || element_size == 2 ||
         element_size == 4 || element_size == 8);
  ASSERT(byteLength == length * element_size);

  void* data = buffer->GetIndexedPropertiesExternalArrayData();
  ASSERT(data != NULL);

  array->SetIndexedPropertiesToExternalArrayData(
      static_cast<uint8_t*>(data) + byteOffset, type, length);
  array->SetHiddenValue(PerIsolateData::ArrayMarkerPropName_string(isolate),
                        Int32::New(type, isolate));
  array->Set(PerIsolateData::byteLength_string(isolate),
             Int32::New(byteLength, isolate),
             ReadOnly);
  array->Set(PerIsolateData::byteOffset_string(isolate),
             Int32::New(byteOffset, isolate),
             ReadOnly);
  array->Set(PerIsolateData::length_string(isolate),
             Int32::New(length, isolate),
             ReadOnly);
  array->Set(PerIsolateData::BYTES_PER_ELEMENT_string(isolate),
             Int32::New(element_size, isolate));
  array->Set(PerIsolateData::buffer_string(isolate),
             buffer,
             ReadOnly);

  return array;
}


Handle<Value> Shell::CreateExternalArray(const Arguments& args,
                                         ExternalArrayType type,
                                         int32_t element_size) {
  Isolate* isolate = args.GetIsolate();
  if (!args.IsConstructCall()) {
    Handle<Value>* rec_args = new Handle<Value>[args.Length()];
    for (int i = 0; i < args.Length(); ++i) rec_args[i] = args[i];
    Handle<Value> result = args.Callee()->NewInstance(args.Length(), rec_args);
    delete[] rec_args;
    return result;
  }

  TryCatch try_catch;
  ASSERT(element_size == 1 || element_size == 2 ||
         element_size == 4 || element_size == 8);

  // All of the following constructors are supported:
  //   TypedArray(unsigned long length)
  //   TypedArray(type[] array)
  //   TypedArray(TypedArray array)
  //   TypedArray(ArrayBuffer buffer,
  //              optional unsigned long byteOffset,
  //              optional unsigned long length)
  Handle<Object> buffer;
  int32_t length;
  int32_t byteLength;
  int32_t byteOffset;
  bool init_from_array = false;
  if (args.Length() == 0) {
    return Throw("Array constructor must have at least one argument");
  }
  if (args[0]->IsObject() &&
      !args[0]->ToObject()->GetHiddenValue(
         PerIsolateData::ArrayBufferMarkerPropName_string(isolate)).IsEmpty()) {
    // Construct from ArrayBuffer.
    buffer = args[0]->ToObject();
    int32_t bufferLength = convertToUint(
        buffer->Get(PerIsolateData::byteLength_string(isolate)), &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();

    if (args.Length() < 2 || args[1]->IsUndefined()) {
      byteOffset = 0;
    } else {
      byteOffset = convertToUint(args[1], &try_catch);
      if (try_catch.HasCaught()) return try_catch.ReThrow();
      if (byteOffset > bufferLength) {
        return Throw("byteOffset out of bounds");
      }
      if (byteOffset % element_size != 0) {
        return Throw("byteOffset must be multiple of element size");
      }
    }

    if (args.Length() < 3 || args[2]->IsUndefined()) {
      byteLength = bufferLength - byteOffset;
      length = byteLength / element_size;
      if (byteLength % element_size != 0) {
        return Throw("buffer size must be multiple of element size");
      }
    } else {
      length = convertToUint(args[2], &try_catch);
      if (try_catch.HasCaught()) return try_catch.ReThrow();
      byteLength = length * element_size;
      if (byteOffset + byteLength > bufferLength) {
        return Throw("length out of bounds");
      }
    }
  } else {
    if (args[0]->IsObject() &&
        args[0]->ToObject()->Has(PerIsolateData::length_string(isolate))) {
      // Construct from array.
      Local<Value> value =
          args[0]->ToObject()->Get(PerIsolateData::length_string(isolate));
      if (try_catch.HasCaught()) return try_catch.ReThrow();
      length = convertToUint(value, &try_catch);
      if (try_catch.HasCaught()) return try_catch.ReThrow();
      init_from_array = true;
    } else {
      // Construct from size.
      length = convertToUint(args[0], &try_catch);
      if (try_catch.HasCaught()) return try_catch.ReThrow();
    }
    byteLength = length * element_size;
    byteOffset = 0;

    Handle<Object> global = Context::GetCurrent()->Global();
    Handle<Value> array_buffer =
        global->Get(PerIsolateData::ArrayBuffer_string(isolate));
    ASSERT(!try_catch.HasCaught() && array_buffer->IsFunction());
    Handle<Value> buffer_args[] = { Uint32::New(byteLength, isolate) };
    Handle<Value> result = Handle<Function>::Cast(array_buffer)->NewInstance(
        1, buffer_args);
    if (try_catch.HasCaught()) return result;
    buffer = result->ToObject();
  }

  Handle<Object> array =
      CreateExternalArray(isolate, args.This(), buffer, type, length,
                          byteLength, byteOffset, element_size);

  if (init_from_array) {
    Handle<Object> init = args[0]->ToObject();
    for (int i = 0; i < length; ++i) {
      Local<Value> value = init->Get(i);
      if (try_catch.HasCaught()) return try_catch.ReThrow();
      array->Set(i, value);
    }
  }

  return array;
}


Handle<Value> Shell::ArrayBufferSlice(const Arguments& args) {
  TryCatch try_catch;

  if (!args.This()->IsObject()) {
    return Throw("'slice' invoked on non-object receiver");
  }

  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This();
  Local<Value> marker = self->GetHiddenValue(
      PerIsolateData::ArrayBufferMarkerPropName_string(isolate));
  if (marker.IsEmpty()) {
    return Throw("'slice' invoked on wrong receiver type");
  }

  int32_t length = convertToUint(
      self->Get(PerIsolateData::byteLength_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();

  if (args.Length() == 0) {
    return Throw("'slice' must have at least one argument");
  }
  int32_t begin = convertToInt(args[0], &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();
  if (begin < 0) begin += length;
  if (begin < 0) begin = 0;
  if (begin > length) begin = length;

  int32_t end;
  if (args.Length() < 2 || args[1]->IsUndefined()) {
    end = length;
  } else {
    end = convertToInt(args[1], &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();
    if (end < 0) end += length;
    if (end < 0) end = 0;
    if (end > length) end = length;
    if (end < begin) end = begin;
  }

  Local<Function> constructor = Local<Function>::Cast(self->GetConstructor());
  Handle<Value> new_args[] = { Uint32::New(end - begin, isolate) };
  Handle<Value> result = constructor->NewInstance(1, new_args);
  if (try_catch.HasCaught()) return result;
  Handle<Object> buffer = result->ToObject();
  uint8_t* dest =
      static_cast<uint8_t*>(buffer->GetIndexedPropertiesExternalArrayData());
  uint8_t* src = begin + static_cast<uint8_t*>(
      self->GetIndexedPropertiesExternalArrayData());
  memcpy(dest, src, end - begin);

  return buffer;
}


Handle<Value> Shell::ArraySubArray(const Arguments& args) {
  TryCatch try_catch;

  if (!args.This()->IsObject()) {
    return Throw("'subarray' invoked on non-object receiver");
  }

  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This();
  Local<Value> marker =
      self->GetHiddenValue(PerIsolateData::ArrayMarkerPropName_string(isolate));
  if (marker.IsEmpty()) {
    return Throw("'subarray' invoked on wrong receiver type");
  }

  Handle<Object> buffer =
      self->Get(PerIsolateData::buffer_string(isolate))->ToObject();
  if (try_catch.HasCaught()) return try_catch.ReThrow();
  int32_t length = convertToUint(
      self->Get(PerIsolateData::length_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();
  int32_t byteOffset = convertToUint(
      self->Get(PerIsolateData::byteOffset_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();
  int32_t element_size = convertToUint(
      self->Get(PerIsolateData::BYTES_PER_ELEMENT_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();

  if (args.Length() == 0) {
    return Throw("'subarray' must have at least one argument");
  }
  int32_t begin = convertToInt(args[0], &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();
  if (begin < 0) begin += length;
  if (begin < 0) begin = 0;
  if (begin > length) begin = length;

  int32_t end;
  if (args.Length() < 2 || args[1]->IsUndefined()) {
    end = length;
  } else {
    end = convertToInt(args[1], &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();
    if (end < 0) end += length;
    if (end < 0) end = 0;
    if (end > length) end = length;
    if (end < begin) end = begin;
  }

  length = end - begin;
  byteOffset += begin * element_size;

  Local<Function> constructor = Local<Function>::Cast(self->GetConstructor());
  Handle<Value> construct_args[] = {
    buffer, Uint32::New(byteOffset, isolate), Uint32::New(length, isolate)
  };
  return constructor->NewInstance(3, construct_args);
}


Handle<Value> Shell::ArraySet(const Arguments& args) {
  TryCatch try_catch;

  if (!args.This()->IsObject()) {
    return Throw("'set' invoked on non-object receiver");
  }

  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This();
  Local<Value> marker =
      self->GetHiddenValue(PerIsolateData::ArrayMarkerPropName_string(isolate));
  if (marker.IsEmpty()) {
    return Throw("'set' invoked on wrong receiver type");
  }
  int32_t length = convertToUint(
      self->Get(PerIsolateData::length_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();
  int32_t element_size = convertToUint(
      self->Get(PerIsolateData::BYTES_PER_ELEMENT_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();

  if (args.Length() == 0) {
    return Throw("'set' must have at least one argument");
  }
  if (!args[0]->IsObject() ||
      !args[0]->ToObject()->Has(PerIsolateData::length_string(isolate))) {
    return Throw("'set' invoked with non-array argument");
  }
  Handle<Object> source = args[0]->ToObject();
  int32_t source_length = convertToUint(
      source->Get(PerIsolateData::length_string(isolate)), &try_catch);
  if (try_catch.HasCaught()) return try_catch.ReThrow();

  int32_t offset;
  if (args.Length() < 2 || args[1]->IsUndefined()) {
    offset = 0;
  } else {
    offset = convertToUint(args[1], &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();
  }
  if (offset + source_length > length) {
    return Throw("offset or source length out of bounds");
  }

  int32_t source_element_size;
  if (source->GetHiddenValue(
          PerIsolateData::ArrayMarkerPropName_string(isolate)).IsEmpty()) {
    source_element_size = 0;
  } else {
    source_element_size = convertToUint(
        source->Get(PerIsolateData::BYTES_PER_ELEMENT_string(isolate)),
        &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();
  }

  if (element_size == source_element_size &&
      self->GetConstructor()->StrictEquals(source->GetConstructor())) {
    // Use memmove on the array buffers.
    Handle<Object> buffer =
        self->Get(PerIsolateData::buffer_string(isolate))->ToObject();
    if (try_catch.HasCaught()) return try_catch.ReThrow();
    Handle<Object> source_buffer =
        source->Get(PerIsolateData::buffer_string(isolate))->ToObject();
    if (try_catch.HasCaught()) return try_catch.ReThrow();
    int32_t byteOffset = convertToUint(
        self->Get(PerIsolateData::byteOffset_string(isolate)), &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();
    int32_t source_byteOffset = convertToUint(
        source->Get(PerIsolateData::byteOffset_string(isolate)), &try_catch);
    if (try_catch.HasCaught()) return try_catch.ReThrow();

    uint8_t* dest = byteOffset + offset * element_size + static_cast<uint8_t*>(
        buffer->GetIndexedPropertiesExternalArrayData());
    uint8_t* src = source_byteOffset + static_cast<uint8_t*>(
        source_buffer->GetIndexedPropertiesExternalArrayData());
    memmove(dest, src, source_length * element_size);
  } else if (source_element_size == 0) {
    // Source is not a typed array, copy element-wise sequentially.
    for (int i = 0; i < source_length; ++i) {
      self->Set(offset + i, source->Get(i));
      if (try_catch.HasCaught()) return try_catch.ReThrow();
    }
  } else {
    // Need to copy element-wise to make the right conversions.
    Handle<Object> buffer =
        self->Get(PerIsolateData::buffer_string(isolate))->ToObject();
    if (try_catch.HasCaught()) return try_catch.ReThrow();
    Handle<Object> source_buffer =
        source->Get(PerIsolateData::buffer_string(isolate))->ToObject();
    if (try_catch.HasCaught()) return try_catch.ReThrow();

    if (buffer->StrictEquals(source_buffer)) {
      // Same backing store, need to handle overlap correctly.
      // This gets a bit tricky in the case of different element sizes
      // (which, of course, is extremely unlikely to ever occur in practice).
      int32_t byteOffset = convertToUint(
          self->Get(PerIsolateData::byteOffset_string(isolate)), &try_catch);
      if (try_catch.HasCaught()) return try_catch.ReThrow();
      int32_t source_byteOffset = convertToUint(
          source->Get(PerIsolateData::byteOffset_string(isolate)), &try_catch);
      if (try_catch.HasCaught()) return try_catch.ReThrow();

      // Copy as much as we can from left to right.
      int i = 0;
      int32_t next_dest_offset = byteOffset + (offset + 1) * element_size;
      int32_t next_src_offset = source_byteOffset + source_element_size;
      while (i < length && next_dest_offset <= next_src_offset) {
        self->Set(offset + i, source->Get(i));
        ++i;
        next_dest_offset += element_size;
        next_src_offset += source_element_size;
      }
      // Of what's left, copy as much as we can from right to left.
      int j = length - 1;
      int32_t dest_offset = byteOffset + (offset + j) * element_size;
      int32_t src_offset = source_byteOffset + j * source_element_size;
      while (j >= i && dest_offset >= src_offset) {
        self->Set(offset + j, source->Get(j));
        --j;
        dest_offset -= element_size;
        src_offset -= source_element_size;
      }
      // There can be at most 8 entries left in the middle that need buffering
      // (because the largest element_size is 8 times the smallest).
      ASSERT(j+1 - i <= 8);
      Handle<Value> temp[8];
      for (int k = i; k <= j; ++k) {
        temp[k - i] = source->Get(k);
      }
      for (int k = i; k <= j; ++k) {
        self->Set(offset + k, temp[k - i]);
      }
    } else {
      // Different backing stores, safe to copy element-wise sequentially.
      for (int i = 0; i < source_length; ++i)
        self->Set(offset + i, source->Get(i));
    }
  }

  return Undefined(args.GetIsolate());
}


void Shell::ExternalArrayWeakCallback(v8::Isolate* isolate,
                                      Persistent<Object>* object,
                                      uint8_t* data) {
  HandleScope scope(isolate);
  int32_t length = (*object)->Get(
      PerIsolateData::byteLength_string(isolate))->Uint32Value();
  isolate->AdjustAmountOfExternalAllocatedMemory(-length);
  delete[] data;
  object->Dispose(isolate);
}


Handle<Value> Shell::Int8Array(const Arguments& args) {
  return CreateExternalArray(args, v8::kExternalByteArray, sizeof(int8_t));
}


Handle<Value> Shell::Uint8Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalUnsignedByteArray, sizeof(uint8_t));
}


Handle<Value> Shell::Int16Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalShortArray, sizeof(int16_t));
}


Handle<Value> Shell::Uint16Array(const Arguments& args) {
  return CreateExternalArray(
      args, kExternalUnsignedShortArray, sizeof(uint16_t));
}


Handle<Value> Shell::Int32Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalIntArray, sizeof(int32_t));
}


Handle<Value> Shell::Uint32Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalUnsignedIntArray, sizeof(uint32_t));
}


Handle<Value> Shell::Float32Array(const Arguments& args) {
  return CreateExternalArray(
      args, kExternalFloatArray, sizeof(float));  // NOLINT
}


Handle<Value> Shell::Float64Array(const Arguments& args) {
  return CreateExternalArray(
      args, kExternalDoubleArray, sizeof(double));  // NOLINT
}


Handle<Value> Shell::Uint8ClampedArray(const Arguments& args) {
  return CreateExternalArray(args, kExternalPixelArray, sizeof(uint8_t));
}


Handle<Value> Shell::Quit(const Arguments& args) {
  int exit_code = args[0]->Int32Value();
  OnExit();
  exit(exit_code);
  return Undefined(args.GetIsolate());
}


Handle<Value> Shell::Version(const Arguments& args) {
  return String::New(V8::GetVersion());
}


void Shell::ReportException(Isolate* isolate, v8::TryCatch* try_catch) {
  HandleScope handle_scope(isolate);
#if !defined(V8_SHARED) && defined(ENABLE_DEBUGGER_SUPPORT)
  bool enter_context = !Context::InContext();
  if (enter_context) utility_context_->Enter();
#endif  // !V8_SHARED && ENABLE_DEBUGGER_SUPPORT
  v8::String::Utf8Value exception(try_catch->Exception());
  const char* exception_string = ToCString(exception);
  Handle<Message> message = try_catch->Message();
  if (message.IsEmpty()) {
    // V8 didn't provide any extra information about this error; just
    // print the exception.
    printf("%s\n", exception_string);
  } else {
    // Print (filename):(line number): (message).
    v8::String::Utf8Value filename(message->GetScriptResourceName());
    const char* filename_string = ToCString(filename);
    int linenum = message->GetLineNumber();
    printf("%s:%i: %s\n", filename_string, linenum, exception_string);
    // Print line of source code.
    v8::String::Utf8Value sourceline(message->GetSourceLine());
    const char* sourceline_string = ToCString(sourceline);
    printf("%s\n", sourceline_string);
    // Print wavy underline (GetUnderline is deprecated).
    int start = message->GetStartColumn();
    for (int i = 0; i < start; i++) {
      printf(" ");
    }
    int end = message->GetEndColumn();
    for (int i = start; i < end; i++) {
      printf("^");
    }
    printf("\n");
    v8::String::Utf8Value stack_trace(try_catch->StackTrace());
    if (stack_trace.length() > 0) {
      const char* stack_trace_string = ToCString(stack_trace);
      printf("%s\n", stack_trace_string);
    }
  }
  printf("\n");
#if !defined(V8_SHARED) && defined(ENABLE_DEBUGGER_SUPPORT)
  if (enter_context) utility_context_->Exit();
#endif  // !V8_SHARED && ENABLE_DEBUGGER_SUPPORT
}


#ifndef V8_SHARED
Handle<Array> Shell::GetCompletions(Isolate* isolate,
                                    Handle<String> text,
                                    Handle<String> full) {
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(isolate, utility_context_);
  Handle<Object> global = utility_context_->Global();
  Handle<Value> fun = global->Get(String::New("GetCompletions"));
  static const int kArgc = 3;
  Handle<Value> argv[kArgc] = { evaluation_context_->Global(), text, full };
  Handle<Value> val = Handle<Function>::Cast(fun)->Call(global, kArgc, argv);
  return handle_scope.Close(Handle<Array>::Cast(val));
}


#ifdef ENABLE_DEBUGGER_SUPPORT
Handle<Object> Shell::DebugMessageDetails(Isolate* isolate,
                                          Handle<String> message) {
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(isolate, utility_context_);
  Handle<Object> global = utility_context_->Global();
  Handle<Value> fun = global->Get(String::New("DebugMessageDetails"));
  static const int kArgc = 1;
  Handle<Value> argv[kArgc] = { message };
  Handle<Value> val = Handle<Function>::Cast(fun)->Call(global, kArgc, argv);
  return Handle<Object>::Cast(val);
}


Handle<Value> Shell::DebugCommandToJSONRequest(Isolate* isolate,
                                               Handle<String> command) {
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(isolate, utility_context_);
  Handle<Object> global = utility_context_->Global();
  Handle<Value> fun = global->Get(String::New("DebugCommandToJSONRequest"));
  static const int kArgc = 1;
  Handle<Value> argv[kArgc] = { command };
  Handle<Value> val = Handle<Function>::Cast(fun)->Call(global, kArgc, argv);
  return val;
}


void Shell::DispatchDebugMessages() {
  Isolate* isolate = v8::Isolate::GetCurrent();
  HandleScope handle_scope(isolate);
  v8::Context::Scope scope(isolate, Shell::evaluation_context_);
  v8::Debug::ProcessDebugMessages();
}
#endif  // ENABLE_DEBUGGER_SUPPORT
#endif  // V8_SHARED


#ifndef V8_SHARED
int32_t* Counter::Bind(const char* name, bool is_histogram) {
  int i;
  for (i = 0; i < kMaxNameSize - 1 && name[i]; i++)
    name_[i] = static_cast<char>(name[i]);
  name_[i] = '\0';
  is_histogram_ = is_histogram;
  return ptr();
}


void Counter::AddSample(int32_t sample) {
  count_++;
  sample_total_ += sample;
}


CounterCollection::CounterCollection() {
  magic_number_ = 0xDEADFACE;
  max_counters_ = kMaxCounters;
  max_name_size_ = Counter::kMaxNameSize;
  counters_in_use_ = 0;
}


Counter* CounterCollection::GetNextCounter() {
  if (counters_in_use_ == kMaxCounters) return NULL;
  return &counters_[counters_in_use_++];
}


void Shell::MapCounters(const char* name) {
  counters_file_ = i::OS::MemoryMappedFile::create(
      name, sizeof(CounterCollection), &local_counters_);
  void* memory = (counters_file_ == NULL) ?
      NULL : counters_file_->memory();
  if (memory == NULL) {
    printf("Could not map counters file %s\n", name);
    Exit(1);
  }
  counters_ = static_cast<CounterCollection*>(memory);
  V8::SetCounterFunction(LookupCounter);
  V8::SetCreateHistogramFunction(CreateHistogram);
  V8::SetAddHistogramSampleFunction(AddHistogramSample);
}


int CounterMap::Hash(const char* name) {
  int h = 0;
  int c;
  while ((c = *name++) != 0) {
    h += h << 5;
    h += c;
  }
  return h;
}


Counter* Shell::GetCounter(const char* name, bool is_histogram) {
  Counter* counter = counter_map_->Lookup(name);

  if (counter == NULL) {
    counter = counters_->GetNextCounter();
    if (counter != NULL) {
      counter_map_->Set(name, counter);
      counter->Bind(name, is_histogram);
    }
  } else {
    ASSERT(counter->is_histogram() == is_histogram);
  }
  return counter;
}


int* Shell::LookupCounter(const char* name) {
  Counter* counter = GetCounter(name, false);

  if (counter != NULL) {
    return counter->ptr();
  } else {
    return NULL;
  }
}


void* Shell::CreateHistogram(const char* name,
                             int min,
                             int max,
                             size_t buckets) {
  return GetCounter(name, true);
}


void Shell::AddHistogramSample(void* histogram, int sample) {
  Counter* counter = reinterpret_cast<Counter*>(histogram);
  counter->AddSample(sample);
}


void Shell::InstallUtilityScript(Isolate* isolate) {
  Locker lock(isolate);
  HandleScope scope(isolate);
  // If we use the utility context, we have to set the security tokens so that
  // utility, evaluation and debug context can all access each other.
  utility_context_->SetSecurityToken(Undefined(isolate));
  evaluation_context_->SetSecurityToken(Undefined(isolate));
  Context::Scope utility_scope(isolate, utility_context_);

#ifdef ENABLE_DEBUGGER_SUPPORT
  if (i::FLAG_debugger) printf("JavaScript debugger enabled\n");
  // Install the debugger object in the utility scope
  i::Debug* debug = i::Isolate::Current()->debug();
  debug->Load();
  i::Handle<i::JSObject> js_debug
      = i::Handle<i::JSObject>(debug->debug_context()->global_object());
  utility_context_->Global()->Set(String::New("$debug"),
                                  Utils::ToLocal(js_debug));
  debug->debug_context()->set_security_token(HEAP->undefined_value());
#endif  // ENABLE_DEBUGGER_SUPPORT

  // Run the d8 shell utility script in the utility context
  int source_index = i::NativesCollection<i::D8>::GetIndex("d8");
  i::Vector<const char> shell_source =
      i::NativesCollection<i::D8>::GetRawScriptSource(source_index);
  i::Vector<const char> shell_source_name =
      i::NativesCollection<i::D8>::GetScriptName(source_index);
  Handle<String> source = String::New(shell_source.start(),
      shell_source.length());
  Handle<String> name = String::New(shell_source_name.start(),
      shell_source_name.length());
  Handle<Script> script = Script::Compile(source, name);
  script->Run();
  // Mark the d8 shell script as native to avoid it showing up as normal source
  // in the debugger.
  i::Handle<i::Object> compiled_script = Utils::OpenHandle(*script);
  i::Handle<i::Script> script_object = compiled_script->IsJSFunction()
      ? i::Handle<i::Script>(i::Script::cast(
          i::JSFunction::cast(*compiled_script)->shared()->script()))
      : i::Handle<i::Script>(i::Script::cast(
          i::SharedFunctionInfo::cast(*compiled_script)->script()));
  script_object->set_type(i::Smi::FromInt(i::Script::TYPE_NATIVE));

#ifdef ENABLE_DEBUGGER_SUPPORT
  // Start the in-process debugger if requested.
  if (i::FLAG_debugger && !i::FLAG_debugger_agent) {
    v8::Debug::SetDebugEventListener(HandleDebugEvent);
  }
#endif  // ENABLE_DEBUGGER_SUPPORT
}
#endif  // V8_SHARED


#ifdef COMPRESS_STARTUP_DATA_BZ2
class BZip2Decompressor : public v8::StartupDataDecompressor {
 public:
  virtual ~BZip2Decompressor() { }

 protected:
  virtual int DecompressData(char* raw_data,
                             int* raw_data_size,
                             const char* compressed_data,
                             int compressed_data_size) {
    ASSERT_EQ(v8::StartupData::kBZip2,
              v8::V8::GetCompressedStartupDataAlgorithm());
    unsigned int decompressed_size = *raw_data_size;
    int result =
        BZ2_bzBuffToBuffDecompress(raw_data,
                                   &decompressed_size,
                                   const_cast<char*>(compressed_data),
                                   compressed_data_size,
                                   0, 1);
    if (result == BZ_OK) {
      *raw_data_size = decompressed_size;
    }
    return result;
  }
};
#endif


Handle<FunctionTemplate> Shell::CreateArrayBufferTemplate(
    InvocationCallback fun) {
  Handle<FunctionTemplate> buffer_template = FunctionTemplate::New(fun);
  Local<Template> proto_template = buffer_template->PrototypeTemplate();
  proto_template->Set(String::New("slice"),
                      FunctionTemplate::New(ArrayBufferSlice));
  return buffer_template;
}


Handle<FunctionTemplate> Shell::CreateArrayTemplate(InvocationCallback fun) {
  Handle<FunctionTemplate> array_template = FunctionTemplate::New(fun);
  Local<Template> proto_template = array_template->PrototypeTemplate();
  proto_template->Set(String::New("set"), FunctionTemplate::New(ArraySet));
  proto_template->Set(String::New("subarray"),
                      FunctionTemplate::New(ArraySubArray));
  return array_template;
}


Handle<ObjectTemplate> Shell::CreateGlobalTemplate(Isolate* isolate) {
  Handle<ObjectTemplate> global_template = ObjectTemplate::New();
  global_template->Set(String::New("print"), FunctionTemplate::New(Print));
  global_template->Set(String::New("write"), FunctionTemplate::New(Write));
  global_template->Set(String::New("read"), FunctionTemplate::New(Read));
  global_template->Set(String::New("readbuffer"),
                       FunctionTemplate::New(ReadBuffer));
  global_template->Set(String::New("readline"),
                       FunctionTemplate::New(ReadLine));
  global_template->Set(String::New("load"), FunctionTemplate::New(Load));
  global_template->Set(String::New("quit"), FunctionTemplate::New(Quit));
  global_template->Set(String::New("version"), FunctionTemplate::New(Version));
  global_template->Set(String::New("enableProfiler"),
                       FunctionTemplate::New(EnableProfiler));
  global_template->Set(String::New("disableProfiler"),
                       FunctionTemplate::New(DisableProfiler));

  // Bind the Realm object.
  Handle<ObjectTemplate> realm_template = ObjectTemplate::New();
  realm_template->Set(String::New("current"),
                      FunctionTemplate::New(RealmCurrent));
  realm_template->Set(String::New("owner"),
                      FunctionTemplate::New(RealmOwner));
  realm_template->Set(String::New("global"),
                      FunctionTemplate::New(RealmGlobal));
  realm_template->Set(String::New("create"),
                      FunctionTemplate::New(RealmCreate));
  realm_template->Set(String::New("dispose"),
                      FunctionTemplate::New(RealmDispose));
  realm_template->Set(String::New("switch"),
                      FunctionTemplate::New(RealmSwitch));
  realm_template->Set(String::New("eval"),
                      FunctionTemplate::New(RealmEval));
  realm_template->SetAccessor(String::New("shared"),
                              RealmSharedGet, RealmSharedSet);
  global_template->Set(String::New("Realm"), realm_template);

  // Bind the handlers for external arrays.
#ifndef V8_SHARED
  if (!i::FLAG_harmony_typed_arrays) {
#endif  // V8_SHARED
    PropertyAttribute attr =
        static_cast<PropertyAttribute>(ReadOnly | DontDelete);
    global_template->Set(PerIsolateData::ArrayBuffer_string(isolate),
                         CreateArrayBufferTemplate(ArrayBuffer), attr);
    global_template->Set(String::New("Int8Array"),
                         CreateArrayTemplate(Int8Array), attr);
    global_template->Set(String::New("Uint8Array"),
                         CreateArrayTemplate(Uint8Array), attr);
    global_template->Set(String::New("Int16Array"),
                         CreateArrayTemplate(Int16Array), attr);
    global_template->Set(String::New("Uint16Array"),
                         CreateArrayTemplate(Uint16Array), attr);
    global_template->Set(String::New("Int32Array"),
                         CreateArrayTemplate(Int32Array), attr);
    global_template->Set(String::New("Uint32Array"),
                         CreateArrayTemplate(Uint32Array), attr);
    global_template->Set(String::New("Float32Array"),
                         CreateArrayTemplate(Float32Array), attr);
    global_template->Set(String::New("Float64Array"),
                         CreateArrayTemplate(Float64Array), attr);
    global_template->Set(String::New("Uint8ClampedArray"),
                         CreateArrayTemplate(Uint8ClampedArray), attr);
#ifndef V8_SHARED
  }
#endif  // V8_SHARED

#if !defined(V8_SHARED) && !defined(_WIN32) && !defined(_WIN64)
  Handle<ObjectTemplate> os_templ = ObjectTemplate::New();
  AddOSMethods(os_templ);
  global_template->Set(String::New("os"), os_templ);
#endif  // V8_SHARED

  return global_template;
}


void Shell::Initialize(Isolate* isolate) {
#ifdef COMPRESS_STARTUP_DATA_BZ2
  BZip2Decompressor startup_data_decompressor;
  int bz2_result = startup_data_decompressor.Decompress();
  if (bz2_result != BZ_OK) {
    fprintf(stderr, "bzip error code: %d\n", bz2_result);
    Exit(1);
  }
#endif

#ifndef V8_SHARED
  Shell::counter_map_ = new CounterMap();
  // Set up counters
  if (i::StrLength(i::FLAG_map_counters) != 0)
    MapCounters(i::FLAG_map_counters);
  if (i::FLAG_dump_counters || i::FLAG_track_gc_object_stats) {
    V8::SetCounterFunction(LookupCounter);
    V8::SetCreateHistogramFunction(CreateHistogram);
    V8::SetAddHistogramSampleFunction(AddHistogramSample);
  }
#endif  // V8_SHARED
}


void Shell::InitializeDebugger(Isolate* isolate) {
  if (options.test_shell) return;
#ifndef V8_SHARED
  Locker lock(isolate);
  HandleScope scope(isolate);
  Handle<ObjectTemplate> global_template = CreateGlobalTemplate(isolate);
  utility_context_.Reset(isolate,
                         Context::New(isolate, NULL, global_template));

#ifdef ENABLE_DEBUGGER_SUPPORT
  // Start the debugger agent if requested.
  if (i::FLAG_debugger_agent) {
    v8::Debug::EnableAgent("d8 shell", i::FLAG_debugger_port, true);
    v8::Debug::SetDebugMessageDispatchHandler(DispatchDebugMessages, true);
  }
#endif  // ENABLE_DEBUGGER_SUPPORT
#endif  // V8_SHARED
}


Local<Context> Shell::CreateEvaluationContext(Isolate* isolate) {
#ifndef V8_SHARED
  // This needs to be a critical section since this is not thread-safe
  i::ScopedLock lock(context_mutex_);
#endif  // V8_SHARED
  // Initialize the global objects
  Handle<ObjectTemplate> global_template = CreateGlobalTemplate(isolate);
  HandleScope handle_scope(isolate);
  Local<Context> context = Context::New(isolate, NULL, global_template);
  ASSERT(!context.IsEmpty());
  Context::Scope scope(context);

#ifndef V8_SHARED
  i::JSArguments js_args = i::FLAG_js_arguments;
  i::Handle<i::FixedArray> arguments_array =
      FACTORY->NewFixedArray(js_args.argc());
  for (int j = 0; j < js_args.argc(); j++) {
    i::Handle<i::String> arg =
        FACTORY->NewStringFromUtf8(i::CStrVector(js_args[j]));
    arguments_array->set(j, *arg);
  }
  i::Handle<i::JSArray> arguments_jsarray =
      FACTORY->NewJSArrayWithElements(arguments_array);
  context->Global()->Set(String::New("arguments"),
                         Utils::ToLocal(arguments_jsarray));
#endif  // V8_SHARED
  return handle_scope.Close(context);
}


void Shell::Exit(int exit_code) {
  // Use _exit instead of exit to avoid races between isolate
  // threads and static destructors.
  fflush(stdout);
  fflush(stderr);
  _exit(exit_code);
}


#ifndef V8_SHARED
struct CounterAndKey {
  Counter* counter;
  const char* key;
};


inline bool operator<(const CounterAndKey& lhs, const CounterAndKey& rhs) {
  return strcmp(lhs.key, rhs.key) < 0;
}
#endif  // V8_SHARED


void Shell::OnExit() {
  LineEditor* line_editor = LineEditor::Get();
  if (line_editor) line_editor->Close();
#ifndef V8_SHARED
  if (i::FLAG_dump_counters) {
    int number_of_counters = 0;
    for (CounterMap::Iterator i(counter_map_); i.More(); i.Next()) {
      number_of_counters++;
    }
    CounterAndKey* counters = new CounterAndKey[number_of_counters];
    int j = 0;
    for (CounterMap::Iterator i(counter_map_); i.More(); i.Next(), j++) {
      counters[j].counter = i.CurrentValue();
      counters[j].key = i.CurrentKey();
    }
    std::sort(counters, counters + number_of_counters);
    printf("+----------------------------------------------------------------+"
           "-------------+\n");
    printf("| Name                                                           |"
           " Value       |\n");
    printf("+----------------------------------------------------------------+"
           "-------------+\n");
    for (j = 0; j < number_of_counters; j++) {
      Counter* counter = counters[j].counter;
      const char* key = counters[j].key;
      if (counter->is_histogram()) {
        printf("| c:%-60s | %11i |\n", key, counter->count());
        printf("| t:%-60s | %11i |\n", key, counter->sample_total());
      } else {
        printf("| %-62s | %11i |\n", key, counter->count());
      }
    }
    printf("+----------------------------------------------------------------+"
           "-------------+\n");
    delete [] counters;
  }
  delete context_mutex_;
  delete counters_file_;
  delete counter_map_;
#endif  // V8_SHARED
}



static FILE* FOpen(const char* path, const char* mode) {
#if defined(_MSC_VER) && (defined(_WIN32) || defined(_WIN64))
  FILE* result;
  if (fopen_s(&result, path, mode) == 0) {
    return result;
  } else {
    return NULL;
  }
#else
  FILE* file = fopen(path, mode);
  if (file == NULL) return NULL;
  struct stat file_stat;
  if (fstat(fileno(file), &file_stat) != 0) return NULL;
  bool is_regular_file = ((file_stat.st_mode & S_IFREG) != 0);
  if (is_regular_file) return file;
  fclose(file);
  return NULL;
#endif
}


static char* ReadChars(Isolate* isolate, const char* name, int* size_out) {
  // Release the V8 lock while reading files.
  v8::Unlocker unlocker(isolate);
  FILE* file = FOpen(name, "rb");
  if (file == NULL) return NULL;

  fseek(file, 0, SEEK_END);
  int size = ftell(file);
  rewind(file);

  char* chars = new char[size + 1];
  chars[size] = '\0';
  for (int i = 0; i < size;) {
    int read = static_cast<int>(fread(&chars[i], 1, size - i, file));
    i += read;
  }
  fclose(file);
  *size_out = size;
  return chars;
}


Handle<Value> Shell::ReadBuffer(const Arguments& args) {
  ASSERT(sizeof(char) == sizeof(uint8_t));  // NOLINT
  String::Utf8Value filename(args[0]);
  int length;
  if (*filename == NULL) {
    return Throw("Error loading file");
  }

  uint8_t* data = reinterpret_cast<uint8_t*>(
      ReadChars(args.GetIsolate(), *filename, &length));
  if (data == NULL) {
    return Throw("Error reading file");
  }
  Isolate* isolate = args.GetIsolate();
  Handle<Object> buffer = Object::New();
  buffer->SetHiddenValue(
      PerIsolateData::ArrayBufferMarkerPropName_string(isolate), True());
  Persistent<Object> persistent_buffer =
      Persistent<Object>::New(isolate, buffer);
  persistent_buffer.MakeWeak(isolate, data, ExternalArrayWeakCallback);
  persistent_buffer.MarkIndependent(isolate);
  isolate->AdjustAmountOfExternalAllocatedMemory(length);

  buffer->SetIndexedPropertiesToExternalArrayData(
      data, kExternalUnsignedByteArray, length);
  buffer->Set(PerIsolateData::byteLength_string(isolate),
      Int32::New(static_cast<int32_t>(length), isolate), ReadOnly);
  return buffer;
}


#ifndef V8_SHARED
static char* ReadToken(char* data, char token) {
  char* next = i::OS::StrChr(data, token);
  if (next != NULL) {
    *next = '\0';
    return (next + 1);
  }

  return NULL;
}


static char* ReadLine(char* data) {
  return ReadToken(data, '\n');
}


static char* ReadWord(char* data) {
  return ReadToken(data, ' ');
}
#endif  // V8_SHARED


// Reads a file into a v8 string.
Handle<String> Shell::ReadFile(Isolate* isolate, const char* name) {
  int size = 0;
  char* chars = ReadChars(isolate, name, &size);
  if (chars == NULL) return Handle<String>();
  Handle<String> result = String::New(chars, size);
  delete[] chars;
  return result;
}


void Shell::RunShell(Isolate* isolate) {
  Locker locker(isolate);
  HandleScope outer_scope(isolate);
  Context::Scope context_scope(isolate, evaluation_context_);
  PerIsolateData::RealmScope realm_scope(PerIsolateData::Get(isolate));
  Handle<String> name = String::New("(d8)");
  LineEditor* console = LineEditor::Get();
  printf("V8 version %s [console: %s]\n", V8::GetVersion(), console->name());
  console->Open(isolate);
  while (true) {
    HandleScope inner_scope(isolate);
    Handle<String> input = console->Prompt(Shell::kPrompt);
    if (input.IsEmpty()) break;
    ExecuteString(isolate, input, name, true, true);
  }
  printf("\n");
}


#ifndef V8_SHARED
class ShellThread : public i::Thread {
 public:
  // Takes ownership of the underlying char array of |files|.
  ShellThread(Isolate* isolate, char* files)
      : Thread("d8:ShellThread"),
        isolate_(isolate), files_(files) { }

  ~ShellThread() {
    delete[] files_;
  }

  virtual void Run();
 private:
  Isolate* isolate_;
  char* files_;
};


void ShellThread::Run() {
  char* ptr = files_;
  while ((ptr != NULL) && (*ptr != '\0')) {
    // For each newline-separated line.
    char* next_line = ReadLine(ptr);

    if (*ptr == '#') {
      // Skip comment lines.
      ptr = next_line;
      continue;
    }

    // Prepare the context for this thread.
    Locker locker(isolate_);
    HandleScope outer_scope(isolate_);
    Local<Context> thread_context =
        Shell::CreateEvaluationContext(isolate_);
    Context::Scope context_scope(thread_context);
    PerIsolateData::RealmScope realm_scope(PerIsolateData::Get(isolate_));

    while ((ptr != NULL) && (*ptr != '\0')) {
      HandleScope inner_scope(isolate_);
      char* filename = ptr;
      ptr = ReadWord(ptr);

      // Skip empty strings.
      if (strlen(filename) == 0) {
        continue;
      }

      Handle<String> str = Shell::ReadFile(isolate_, filename);
      if (str.IsEmpty()) {
        printf("File '%s' not found\n", filename);
        Shell::Exit(1);
      }

      Shell::ExecuteString(isolate_, str, String::New(filename), false, false);
    }

    ptr = next_line;
  }
}
#endif  // V8_SHARED


SourceGroup::~SourceGroup() {
#ifndef V8_SHARED
  delete next_semaphore_;
  next_semaphore_ = NULL;
  delete done_semaphore_;
  done_semaphore_ = NULL;
  delete thread_;
  thread_ = NULL;
#endif  // V8_SHARED
}


void SourceGroup::Execute(Isolate* isolate) {
  for (int i = begin_offset_; i < end_offset_; ++i) {
    const char* arg = argv_[i];
    if (strcmp(arg, "-e") == 0 && i + 1 < end_offset_) {
      // Execute argument given to -e option directly.
      HandleScope handle_scope(isolate);
      Handle<String> file_name = String::New("unnamed");
      Handle<String> source = String::New(argv_[i + 1]);
      if (!Shell::ExecuteString(isolate, source, file_name, false, true)) {
        Shell::Exit(1);
      }
      ++i;
    } else if (arg[0] == '-') {
      // Ignore other options. They have been parsed already.
    } else {
      // Use all other arguments as names of files to load and run.
      HandleScope handle_scope(isolate);
      Handle<String> file_name = String::New(arg);
      Handle<String> source = ReadFile(isolate, arg);
      if (source.IsEmpty()) {
        printf("Error reading '%s'\n", arg);
        Shell::Exit(1);
      }
      if (!Shell::ExecuteString(isolate, source, file_name, false, true)) {
        Shell::Exit(1);
      }
    }
  }
}


Handle<String> SourceGroup::ReadFile(Isolate* isolate, const char* name) {
  int size;
  char* chars = ReadChars(isolate, name, &size);
  if (chars == NULL) return Handle<String>();
  Handle<String> result = String::New(chars, size);
  delete[] chars;
  return result;
}


#ifndef V8_SHARED
i::Thread::Options SourceGroup::GetThreadOptions() {
  // On some systems (OSX 10.6) the stack size default is 0.5Mb or less
  // which is not enough to parse the big literal expressions used in tests.
  // The stack size should be at least StackGuard::kLimitSize + some
  // OS-specific padding for thread startup code.  2Mbytes seems to be enough.
  return i::Thread::Options("IsolateThread", 2 * MB);
}


void SourceGroup::ExecuteInThread() {
  Isolate* isolate = Isolate::New();
  do {
    if (next_semaphore_ != NULL) next_semaphore_->Wait();
    {
      Isolate::Scope iscope(isolate);
      Locker lock(isolate);
      {
        HandleScope scope(isolate);
        PerIsolateData data(isolate);
        Local<Context> context = Shell::CreateEvaluationContext(isolate);
        {
          Context::Scope cscope(context);
          PerIsolateData::RealmScope realm_scope(PerIsolateData::Get(isolate));
          Execute(isolate);
        }
      }
      if (Shell::options.send_idle_notification) {
        const int kLongIdlePauseInMs = 1000;
        V8::ContextDisposedNotification();
        V8::IdleNotification(kLongIdlePauseInMs);
      }
    }
    if (done_semaphore_ != NULL) done_semaphore_->Signal();
  } while (!Shell::options.last_run);
  isolate->Dispose();
}


void SourceGroup::StartExecuteInThread() {
  if (thread_ == NULL) {
    thread_ = new IsolateThread(this);
    thread_->Start();
  }
  next_semaphore_->Signal();
}


void SourceGroup::WaitForThread() {
  if (thread_ == NULL) return;
  if (Shell::options.last_run) {
    thread_->Join();
  } else {
    done_semaphore_->Wait();
  }
}
#endif  // V8_SHARED


bool Shell::SetOptions(int argc, char* argv[]) {
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--stress-opt") == 0) {
      options.stress_opt = true;
      argv[i] = NULL;
    } else if (strcmp(argv[i], "--stress-deopt") == 0) {
      options.stress_deopt = true;
      argv[i] = NULL;
    } else if (strcmp(argv[i], "--noalways-opt") == 0) {
      // No support for stressing if we can't use --always-opt.
      options.stress_opt = false;
      options.stress_deopt = false;
    } else if (strcmp(argv[i], "--shell") == 0) {
      options.interactive_shell = true;
      argv[i] = NULL;
    } else if (strcmp(argv[i], "--test") == 0) {
      options.test_shell = true;
      argv[i] = NULL;
    } else if (strcmp(argv[i], "--send-idle-notification") == 0) {
      options.send_idle_notification = true;
      argv[i] = NULL;
    } else if (strcmp(argv[i], "--preemption") == 0) {
#ifdef V8_SHARED
      printf("D8 with shared library does not support multi-threading\n");
      return false;
#else
      options.use_preemption = true;
      argv[i] = NULL;
#endif  // V8_SHARED
    } else if (strcmp(argv[i], "--nopreemption") == 0) {
#ifdef V8_SHARED
      printf("D8 with shared library does not support multi-threading\n");
      return false;
#else
      options.use_preemption = false;
      argv[i] = NULL;
#endif  // V8_SHARED
    } else if (strcmp(argv[i], "--preemption-interval") == 0) {
#ifdef V8_SHARED
      printf("D8 with shared library does not support multi-threading\n");
      return false;
#else
      if (++i < argc) {
        argv[i-1] = NULL;
        char* end = NULL;
        options.preemption_interval = strtol(argv[i], &end, 10);  // NOLINT
        if (options.preemption_interval <= 0
            || *end != '\0'
            || errno == ERANGE) {
          printf("Invalid value for --preemption-interval '%s'\n", argv[i]);
          return false;
        }
        argv[i] = NULL;
      } else {
        printf("Missing value for --preemption-interval\n");
        return false;
      }
#endif  // V8_SHARED
    } else if (strcmp(argv[i], "-f") == 0) {
      // Ignore any -f flags for compatibility with other stand-alone
      // JavaScript engines.
      continue;
    } else if (strcmp(argv[i], "--isolate") == 0) {
#ifdef V8_SHARED
      printf("D8 with shared library does not support multi-threading\n");
      return false;
#endif  // V8_SHARED
      options.num_isolates++;
    } else if (strcmp(argv[i], "-p") == 0) {
#ifdef V8_SHARED
      printf("D8 with shared library does not support multi-threading\n");
      return false;
#else
      options.num_parallel_files++;
#endif  // V8_SHARED
    }
#ifdef V8_SHARED
    else if (strcmp(argv[i], "--dump-counters") == 0) {
      printf("D8 with shared library does not include counters\n");
      return false;
    } else if (strcmp(argv[i], "--debugger") == 0) {
      printf("Javascript debugger not included\n");
      return false;
    }
#endif  // V8_SHARED
  }

#ifndef V8_SHARED
  // Run parallel threads if we are not using --isolate
  options.parallel_files = new char*[options.num_parallel_files];
  int parallel_files_set = 0;
  for (int i = 1; i < argc; i++) {
    if (argv[i] == NULL) continue;
    if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
      if (options.num_isolates > 1) {
        printf("-p is not compatible with --isolate\n");
        return false;
      }
      argv[i] = NULL;
      i++;
      options.parallel_files[parallel_files_set] = argv[i];
      parallel_files_set++;
      argv[i] = NULL;
    }
  }
  if (parallel_files_set != options.num_parallel_files) {
    printf("-p requires a file containing a list of files as parameter\n");
    return false;
  }
#endif  // V8_SHARED

  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

  // Set up isolated source groups.
  options.isolate_sources = new SourceGroup[options.num_isolates];
  SourceGroup* current = options.isolate_sources;
  current->Begin(argv, 1);
  for (int i = 1; i < argc; i++) {
    const char* str = argv[i];
    if (strcmp(str, "--isolate") == 0) {
      current->End(i);
      current++;
      current->Begin(argv, i + 1);
    } else if (strncmp(argv[i], "--", 2) == 0) {
      printf("Warning: unknown flag %s.\nTry --help for options\n", argv[i]);
    }
  }
  current->End(argc);

  return true;
}


int Shell::RunMain(Isolate* isolate, int argc, char* argv[]) {
#ifndef V8_SHARED
  i::List<i::Thread*> threads(1);
  if (options.parallel_files != NULL) {
    for (int i = 0; i < options.num_parallel_files; i++) {
      char* files = NULL;
      { Locker lock(isolate);
        int size = 0;
        files = ReadChars(isolate, options.parallel_files[i], &size);
      }
      if (files == NULL) {
        printf("File list '%s' not found\n", options.parallel_files[i]);
        Exit(1);
      }
      ShellThread* thread = new ShellThread(isolate, files);
      thread->Start();
      threads.Add(thread);
    }
  }
  for (int i = 1; i < options.num_isolates; ++i) {
    options.isolate_sources[i].StartExecuteInThread();
  }
#endif  // V8_SHARED
  {  // NOLINT
    Locker lock(isolate);
    {
      HandleScope scope(isolate);
      Local<Context> context = CreateEvaluationContext(isolate);
      if (options.last_run) {
        // Keep using the same context in the interactive shell.
        evaluation_context_.Reset(isolate, context);
#if !defined(V8_SHARED) && defined(ENABLE_DEBUGGER_SUPPORT)
        // If the interactive debugger is enabled make sure to activate
        // it before running the files passed on the command line.
        if (i::FLAG_debugger) {
          InstallUtilityScript(isolate);
        }
#endif  // !V8_SHARED && ENABLE_DEBUGGER_SUPPORT
      }
      {
        Context::Scope cscope(context);
        PerIsolateData::RealmScope realm_scope(PerIsolateData::Get(isolate));
        options.isolate_sources[0].Execute(isolate);
      }
    }
    if (!options.last_run) {
      if (options.send_idle_notification) {
        const int kLongIdlePauseInMs = 1000;
        V8::ContextDisposedNotification();
        V8::IdleNotification(kLongIdlePauseInMs);
      }
    }

#ifndef V8_SHARED
    // Start preemption if threads have been created and preemption is enabled.
    if (threads.length() > 0
        && options.use_preemption) {
      Locker::StartPreemption(options.preemption_interval);
    }
#endif  // V8_SHARED
  }

#ifndef V8_SHARED
  for (int i = 1; i < options.num_isolates; ++i) {
    options.isolate_sources[i].WaitForThread();
  }

  for (int i = 0; i < threads.length(); i++) {
    i::Thread* thread = threads[i];
    thread->Join();
    delete thread;
  }

  if (threads.length() > 0 && options.use_preemption) {
    Locker lock(isolate);
    Locker::StopPreemption();
  }
#endif  // V8_SHARED
  return 0;
}


#ifdef V8_SHARED
static void EnableHarmonyTypedArraysViaCommandLine() {
  int fake_argc = 2;
  char **fake_argv = new char*[2];
  fake_argv[0] = NULL;
  fake_argv[1] = strdup("--harmony-typed-arrays");
  v8::V8::SetFlagsFromCommandLine(&fake_argc, fake_argv, false);
  free(fake_argv[1]);
  delete[] fake_argv;
}
#endif


int Shell::Main(int argc, char* argv[]) {
  if (!SetOptions(argc, argv)) return 1;
#ifndef V8_SHARED
  i::FLAG_harmony_array_buffer = true;
  i::FLAG_harmony_typed_arrays = true;
#else
  EnableHarmonyTypedArraysViaCommandLine();
#endif
  int result = 0;
  Isolate* isolate = Isolate::GetCurrent();
  DumbLineEditor dumb_line_editor(isolate);
  {
    Initialize(isolate);
#ifdef ENABLE_VTUNE_JIT_INTERFACE
    vTune::InitializeVtuneForV8();
#endif
    PerIsolateData data(isolate);
    InitializeDebugger(isolate);

    if (options.stress_opt || options.stress_deopt) {
      Testing::SetStressRunType(options.stress_opt
                                ? Testing::kStressTypeOpt
                                : Testing::kStressTypeDeopt);
      int stress_runs = Testing::GetStressRuns();
      for (int i = 0; i < stress_runs && result == 0; i++) {
        printf("============ Stress %d/%d ============\n", i + 1, stress_runs);
        Testing::PrepareStressRun(i);
        options.last_run = (i == stress_runs - 1);
        result = RunMain(isolate, argc, argv);
      }
      printf("======== Full Deoptimization =======\n");
      Testing::DeoptimizeAll();
#if !defined(V8_SHARED)
    } else if (i::FLAG_stress_runs > 0) {
      int stress_runs = i::FLAG_stress_runs;
      for (int i = 0; i < stress_runs && result == 0; i++) {
        printf("============ Run %d/%d ============\n", i + 1, stress_runs);
        options.last_run = (i == stress_runs - 1);
        result = RunMain(isolate, argc, argv);
      }
#endif
    } else {
      result = RunMain(isolate, argc, argv);
    }


#if !defined(V8_SHARED) && defined(ENABLE_DEBUGGER_SUPPORT)
    // Run remote debugger if requested, but never on --test
    if (i::FLAG_remote_debugger && !options.test_shell) {
      InstallUtilityScript(isolate);
      RunRemoteDebugger(isolate, i::FLAG_debugger_port);
      return 0;
    }
#endif  // !V8_SHARED && ENABLE_DEBUGGER_SUPPORT

    // Run interactive shell if explicitly requested or if no script has been
    // executed, but never on --test

    if (( options.interactive_shell || !options.script_executed )
        && !options.test_shell ) {
#if !defined(V8_SHARED) && defined(ENABLE_DEBUGGER_SUPPORT)
      if (!i::FLAG_debugger) {
        InstallUtilityScript(isolate);
      }
#endif  // !V8_SHARED && ENABLE_DEBUGGER_SUPPORT
      RunShell(isolate);
    }
  }
  V8::Dispose();

  OnExit();

  return result;
}

}  // namespace v8


#ifndef GOOGLE3
int main(int argc, char* argv[]) {
  return v8::Shell::Main(argc, argv);
}
#endif
