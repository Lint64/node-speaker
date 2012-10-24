#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "v8.h"
#include "node.h"
#include "node_buffer.h"
#include "node_pointer.h"
#include "mpg123app.h"

using namespace v8;
using namespace node;

extern mpg123_module_t mpg123_output_module_info;

namespace {

struct write_req {
  uv_work_t req;
  audio_output_t *ao;
  unsigned char* buffer;
  int len;
  int written;
  Persistent<Function> callback;
};

Handle<Value> Open (const Arguments& args) {
  HandleScope scope;
  int r;
  audio_output_t *ao = reinterpret_cast<audio_output_t *>(UnwrapPointer(args[0]));
  memset(ao, 0, sizeof(audio_output_t));

  /* TODO: configurable */
  ao->channels = 2; /* channels */
  ao->rate = 44100; /* rample rate */
  ao->format = MPG123_ENC_SIGNED_16; /* bit depth, is signed?, int/float */

  /* init_output() */
  r = mpg123_output_module_info.init_output(ao);
  if (r) {
    fprintf(stderr, "init_output() failed: %d\n", r);
    return scope.Close(Integer::New(r));
  }

  /* open() */
  r = ao->open(ao);

  return scope.Close(Integer::New(r));
}

void write_async (uv_work_t *);
void write_after (uv_work_t *);

Handle<Value> Write (const Arguments& args) {
  HandleScope scope;
  audio_output_t *ao = reinterpret_cast<audio_output_t *>(UnwrapPointer(args[0]));
  unsigned char *buffer = reinterpret_cast<unsigned char *>(UnwrapPointer(args[1]));
  int len = args[2]->Int32Value();
  Local<Function> callback = Local<Function>::Cast(args[3]);

  write_req *req = new write_req;
  req->ao = ao;
  req->buffer = buffer;
  req->len = len;
  req->written = 0;
  req->callback = Persistent<Function>::New(callback);

  req->req.data = req;

  uv_queue_work(uv_default_loop(), &req->req, write_async, write_after);

  return Undefined();
}

void write_async (uv_work_t *req) {
  write_req *wreq = reinterpret_cast<write_req *>(req->data);
  wreq->written = wreq->ao->write(wreq->ao, wreq->buffer, wreq->len);
}

void write_after (uv_work_t *req) {
  HandleScope scope;
  write_req *wreq = reinterpret_cast<write_req *>(req->data);

  Handle<Value> argv[1] = { Integer::New(wreq->written) };

  TryCatch try_catch;

  wreq->callback->Call(Context::GetCurrent()->Global(), 1, argv);

  // cleanup
  wreq->callback.Dispose();
  delete wreq;

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }
}

Handle<Value> Flush (const Arguments& args) {
  HandleScope scope;
  audio_output_t *ao = reinterpret_cast<audio_output_t *>(UnwrapPointer(args[0]));
  /* TODO: async */
  ao->flush(ao);
  return Undefined();
}

Handle<Value> Close (const Arguments& args) {
  HandleScope scope;
  audio_output_t *ao = reinterpret_cast<audio_output_t *>(UnwrapPointer(args[0]));
  ao->close(ao);
  int r = 0;
  r = ao->deinit(ao);
  return scope.Close(Integer::New(r));
}

void Initialize(Handle<Object> target) {
  HandleScope scope;
  target->Set(String::NewSymbol("api_version"), Integer::New(mpg123_output_module_info.api_version));
  target->Set(String::NewSymbol("name"), String::New(mpg123_output_module_info.name));
  target->Set(String::NewSymbol("description"), String::New(mpg123_output_module_info.description));
  target->Set(String::NewSymbol("revision"), String::New(mpg123_output_module_info.revision));

  target->Set(String::NewSymbol("sizeof_audio_output_t"), Integer::New(sizeof(audio_output_t)));

  NODE_SET_METHOD(target, "open", Open);
  NODE_SET_METHOD(target, "write", Write);
  NODE_SET_METHOD(target, "flush", Flush);
  NODE_SET_METHOD(target, "close", Close);
}

} // anonymous namespace

NODE_MODULE(binding, Initialize)
