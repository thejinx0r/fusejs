// Copyright 2012, Camilo Aguilar. Cloudescape, LLC.
#include <stdlib.h>

#include "bindings.h"
uv_async_t uv_async_handle;
namespace NodeFuse {
    Persistent<FunctionTemplate> Fuse::constructor_template;

    //static Persistent<String> mountpoint_sym;
    static Persistent<String> filesystem_sym;
    static Persistent<String> options_sym;

    void Fuse::Initialize(Handle<Object> target) {
        Local<FunctionTemplate> t = NanNew<FunctionTemplate>(Fuse::New);

        t->InstanceTemplate()->SetInternalFieldCount(1);

        NODE_SET_PROTOTYPE_METHOD(t, "mount",
                                      Fuse::Mount);
        //NODE_SET_PROTOTYPE_METHOD(t, "unmount",
        //                              Fuse::Unmount);

        NanAssignPersistent(constructor_template, t);
        // constructor_template->SetClassName(String::NewSymbol("Fuse"));

        target->Set(NanNew("fuse_version"), NanNew<Integer>(fuse_version()));
        target->Set(NanNew("Fuse"), NanNew(constructor_template)->GetFunction());

        //mountpoint_sym        = NODE_PSYMBOL("mountpoint");
        NanAssignPersistent(filesystem_sym, NanNew("filesystem"));
        NanAssignPersistent(options_sym, NanNew("options"));
    }

    Fuse::Fuse() : ObjectWrap() {}
    Fuse::~Fuse() {
        if (fargs != NULL) {
            fuse_opt_free_args(fargs);
        }

        if (session != NULL) {
            fuse_remove_signal_handlers(session);
        }

        if (channel != NULL) {
            fuse_unmount(mountpoint, channel);
            fuse_session_remove_chan(channel);
        }

        if (mountpoint != NULL) {
            free(mountpoint);
        }
    }

    NAN_METHOD( Fuse::New) {
        NanEscapableScope();

        Fuse *fuse = new Fuse();
        Local<Object> obj = args.This();
        fuse->Wrap(obj);

        NanReturnValue( obj );
    }

    void Fuse::RemoteMount(void *_args_) {
        Fuse *fuse  = (Fuse *) _args_; 
        int ret;
        // fuse->channel = fuse_mount((const char*) fuse->mountpoint, fuse->fargs);
        // if (fuse->channel == NULL) {
        //     FUSEJS_THROW_EXCEPTION("Unable to mount filesystem: ", strerror(errno));
        //     return;
        // }

        struct fuse_lowlevel_ops *operations = FileSystem::GetOperations();

        fuse->session = fuse_lowlevel_new(fuse->fargs, operations,
                                            sizeof(*operations), fuse);

        if (fuse->session == NULL) {
            fuse_unmount(fuse->mountpoint, fuse->channel);
            fuse_opt_free_args(fuse->fargs);
            //FUSEJS_THROW_EXCEPTION("Error creating fuse session: ", strerror(errno));
            return;
        }

        ret = fuse_set_signal_handlers(fuse->session);
        if (ret == -1) {
            fuse_session_destroy(fuse->session);
            fuse_unmount(fuse->mountpoint, fuse->channel);
            fuse_opt_free_args(fuse->fargs);
            FUSEJS_THROW_EXCEPTION("Error setting fuse signal handlers: ", strerror(errno));
            return;
        }

        fuse_session_add_chan(fuse->session, fuse->channel);

        ret = fuse_session_loop(fuse->session); //blocks here

        //Continues executing if user unmounts the fs
        fuse_remove_signal_handlers(fuse->session);
        fuse_unmount(fuse->mountpoint, fuse->channel);
        fuse_session_remove_chan(fuse->channel);
        fuse_session_destroy(fuse->session);
        // fuse_opt_free_args(fuse->fargs);

        if (ret == -1) {
            FUSEJS_THROW_EXCEPTION("Error starting fuse session loop: ", strerror(errno));
            return;
        }

        return;
    }
    
    NAN_METHOD(Fuse::Mount) {
        NanEscapableScope();

        int argslen = args.Length();

        if (argslen == 0) {
            NanThrowError(
            "You must specify arguments to invoke this function");
            NanReturnUndefined();
        }

        if (!args[0]->IsObject()) {
            NanThrowError(
            "You must specify an Object as first argument");
            NanReturnUndefined();
        }

        Local<Object> argsObj = args[0]->ToObject();
        //THROW_IF_MISSING_PROPERTY(argsObj, mountpoint_sym, "mountpoint");
        THROW_IF_MISSING_PROPERTY(argsObj, filesystem_sym, "filesystem");
        THROW_IF_MISSING_PROPERTY(argsObj, options_sym, "options");

        //Local<Value> vmountpoint = argsObj->Get(mountpoint_sym);
        Local<Value> vfilesystem = argsObj->Get(NanNew(filesystem_sym));
        Local<Value> voptions = argsObj->Get(NanNew(options_sym));

        if (!vfilesystem->IsFunction()) {
            NanThrowError(
                "Wrong type for property 'filesystem', a Function is expected");
            NanReturnUndefined();
        }

        if (!voptions->IsArray()) {
            NanThrowError(
                "Wrong type for property 'options', an Array is expected");
            NanReturnUndefined();
        }

        Local<Array> options = Local<Array>::Cast(voptions);
        int argc = options->Length();

        //If no mountpoint is provided, show usage.
        if (argc < 1) {
            options->Set(NanNew<Integer>(1), NanNew<String>("--help"));
            argc++;
        }

        Local<Object> currentInstance = args.This();
        Fuse *fuse  = ObjectWrap::Unwrap<Fuse>(currentInstance);
        struct fuse_args fargs = FUSE_ARGS_INIT(0, NULL);
        fuse->fargs = (struct fuse_args *)malloc(sizeof(fargs));
        memcpy( fuse->fargs, &fargs, sizeof(fargs) );   

        for (int i = 0; i < argc; i++) {
            String::Utf8Value option(options->Get(NanNew<Integer>(i))->ToString());
            char *fopt = strdup(*option);
            if (fuse_opt_add_arg(fuse->fargs, (const char *) fopt) == -1) {
                FUSEJS_THROW_EXCEPTION("Unable to allocate memory, fuse_opt_add_arg failed: ", strerror(errno));
                NanReturnUndefined();
            }
        }

        //String::Utf8Value mountpoint(vmountpoint->ToString());
        //fuse->mountpoint = *mountpoint;

        int ret = fuse_parse_cmdline(fuse->fargs, &fuse->mountpoint,
                                        &fuse->multithreaded, &fuse->foreground);
        if (ret == -1) {
            FUSEJS_THROW_EXCEPTION("Error parsing fuse options: ", strerror(errno));
            NanReturnUndefined();
        }

        if (!fuse->mountpoint) {
            FUSEJS_THROW_EXCEPTION("Mount point argument was not found", "");
            NanReturnUndefined();
        }

        fuse->channel = fuse_mount((const char*) fuse->mountpoint, fuse->fargs);
        if (fuse->channel == NULL) {
            FUSEJS_THROW_EXCEPTION("Unable to mount filesystem: ", strerror(errno));
            NanReturnUndefined();
        }

        Local<Value> argv[2] = {
            currentInstance,
            options
        };

        Local<Function> filesystem = Local<Function>::Cast(vfilesystem);
        NanAssignPersistent(fuse->fsobj, NanNew(filesystem)->NewInstance(2, argv) );
        assert(NanNew(fuse->fsobj)->IsObject());
        assert(NanNew(fuse->fsobj)->Get(NanNew("init"))->IsFunction());

        uv_async_init(uv_default_loop(), &uv_async_handle, (uv_async_cb) FileSystem::DispatchOp);
        uv_thread_t fuse_thread;
        uv_thread_create(&fuse_thread, Fuse::RemoteMount, (void *) fuse);


        NanReturnThis();
    }



    /*Handle<Value> Fuse::Unmount(const Arguments& args) {
        HandleScope scope;

        Local<Object> currentInstance = args.This();
        Fuse *fuse = ObjectWrap::Unwrap<Fuse>(currentInstance);

        fuse_session_remove_chan(fuse->channel);
        fuse_remove_signal_handlers(fuse->session);
        fuse_session_destroy(fuse->session);
        fuse_unmount(fuse->mountpoint, fuse->channel);

        return currentInstance;
    }*/
} //namespace NodeFuse

