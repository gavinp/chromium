// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/plugins/ppapi/ppb_file_chooser_impl.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/shared_impl/tracked_callback.h"
#include "ppapi/shared_impl/var.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebCString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileChooserCompletion.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFileChooserParams.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebVector.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/plugins/ppapi/common.h"
#include "webkit/plugins/ppapi/ppb_file_ref_impl.h"
#include "webkit/plugins/ppapi/plugin_delegate.h"
#include "webkit/plugins/ppapi/plugin_module.h"
#include "webkit/plugins/ppapi/ppapi_plugin_instance.h"
#include "webkit/plugins/ppapi/resource_helper.h"

using ppapi::StringVar;
using ppapi::thunk::PPB_FileChooser_API;
using ppapi::TrackedCallback;
using WebKit::WebCString;
using WebKit::WebFileChooserCompletion;
using WebKit::WebFileChooserParams;
using WebKit::WebString;
using WebKit::WebVector;

namespace webkit {
namespace ppapi {

namespace {

class FileChooserCompletionImpl : public WebFileChooserCompletion {
 public:
  FileChooserCompletionImpl(PPB_FileChooser_Impl* file_chooser)
      : file_chooser_(file_chooser) {
    DCHECK(file_chooser_);
  }

  virtual ~FileChooserCompletionImpl() {}

  virtual void didChooseFile(const WebVector<WebString>& file_names) {
    std::vector<std::string> files;
    for (size_t i = 0; i < file_names.size(); i++)
      files.push_back(file_names[i].utf8());

    file_chooser_->StoreChosenFiles(files);
  }
  virtual void didChooseFile(const WebVector<SelectedFileInfo>& file_names) {
    std::vector<std::string> files;
    for (size_t i = 0; i < file_names.size(); i++)
      files.push_back(file_names[i].path.utf8());

    file_chooser_->StoreChosenFiles(files);
  }

 private:
  scoped_refptr<PPB_FileChooser_Impl> file_chooser_;
};

}  // namespace

PPB_FileChooser_Impl::PPB_FileChooser_Impl(
    PP_Instance instance,
    PP_FileChooserMode_Dev mode,
    const char* accept_mime_types)
    : Resource(::ppapi::OBJECT_IS_IMPL, instance),
      mode_(mode),
      next_chosen_file_index_(0) {
  if (accept_mime_types)
    accept_mime_types_ = std::string(accept_mime_types);
}

PPB_FileChooser_Impl::~PPB_FileChooser_Impl() {
}

// static
PP_Resource PPB_FileChooser_Impl::Create(
    PP_Instance instance,
    PP_FileChooserMode_Dev mode,
    const char* accept_mime_types) {
  if (mode != PP_FILECHOOSERMODE_OPEN &&
      mode != PP_FILECHOOSERMODE_OPENMULTIPLE)
    return 0;
  return (new PPB_FileChooser_Impl(instance, mode,
                                   accept_mime_types))->GetReference();
}

PPB_FileChooser_Impl* PPB_FileChooser_Impl::AsPPB_FileChooser_Impl() {
  return this;
}

PPB_FileChooser_API* PPB_FileChooser_Impl::AsPPB_FileChooser_API() {
  return this;
}

void PPB_FileChooser_Impl::StoreChosenFiles(
    const std::vector<std::string>& files) {
  next_chosen_file_index_ = 0;

  std::vector< scoped_refptr<Resource> > chosen_files;
  for (std::vector<std::string>::const_iterator it = files.begin();
       it != files.end(); ++it) {
#if defined(OS_WIN)
    FilePath file_path(base::SysUTF8ToWide(*it));
#else
    FilePath file_path(*it);
#endif

    chosen_files.push_back(scoped_refptr<Resource>(
        PPB_FileRef_Impl::CreateExternal(pp_instance(), file_path)));
  }

  int32_t result_code = (chosen_files.size() > 0) ? PP_OK : PP_ERROR_USERCANCEL;
  if (output_.is_valid())
    output_.StoreResourceVector(chosen_files);
  else  // v0.5 API.
    chosen_files_.swap(chosen_files);
  RunCallback(result_code);
}

int32_t PPB_FileChooser_Impl::ValidateCallback(
    const PP_CompletionCallback& callback) {
  // We only support non-blocking calls.
  if (!callback.func)
    return PP_ERROR_BLOCKS_MAIN_THREAD;

  if (TrackedCallback::IsPending(callback_))
    return PP_ERROR_INPROGRESS;

  return PP_OK;
}

void PPB_FileChooser_Impl::RegisterCallback(
    const PP_CompletionCallback& callback) {
  DCHECK(callback.func);
  DCHECK(!TrackedCallback::IsPending(callback_));

  PluginModule* plugin_module = ResourceHelper::GetPluginModule(this);
  if (!plugin_module)
    return;

  callback_ = new TrackedCallback(this, callback);
}

void PPB_FileChooser_Impl::RunCallback(int32_t result) {
  TrackedCallback::ClearAndRun(&callback_, result);
}

int32_t PPB_FileChooser_Impl::Show(const PP_ArrayOutput& output,
                                   const PP_CompletionCallback& callback) {
  int32_t result = Show0_5(callback);
  if (result == PP_OK_COMPLETIONPENDING)
    output_.set_pp_array_output(output);
  return result;
}

int32_t PPB_FileChooser_Impl::ShowWithoutUserGesture(
    PP_Bool save_as,
    PP_Var suggested_file_name,
    const PP_ArrayOutput& output,
    const PP_CompletionCallback& callback) {
  int32_t result = ShowWithoutUserGesture0_5(save_as, suggested_file_name,
                                             callback);
  if (result == PP_OK_COMPLETIONPENDING)
    output_.set_pp_array_output(output);
  return result;
}

int32_t PPB_FileChooser_Impl::Show0_5(const PP_CompletionCallback& callback) {
  PluginInstance* plugin_instance = ResourceHelper::GetPluginInstance(this);
  if (!plugin_instance)
    return PP_ERROR_FAILED;
  if (!plugin_instance->IsProcessingUserGesture())
    return PP_ERROR_NO_USER_GESTURE;
  return ShowWithoutUserGesture0_5(PP_FALSE, PP_MakeUndefined(), callback);
}

int32_t PPB_FileChooser_Impl::ShowWithoutUserGesture0_5(
    PP_Bool save_as,
    PP_Var suggested_file_name,
    const PP_CompletionCallback& callback) {
  int32_t rv = ValidateCallback(callback);
  if (rv != PP_OK)
    return rv;

  DCHECK((mode_ == PP_FILECHOOSERMODE_OPEN) ||
         (mode_ == PP_FILECHOOSERMODE_OPENMULTIPLE));

  WebFileChooserParams params;
  if (save_as) {
    params.saveAs = true;
    StringVar* str = StringVar::FromPPVar(suggested_file_name);
    if (str)
      params.initialValue = WebString::fromUTF8(str->value().c_str());
  } else {
    params.multiSelect = (mode_ == PP_FILECHOOSERMODE_OPENMULTIPLE);
  }
  params.acceptMIMETypes = ParseAcceptValue(accept_mime_types_);
  params.directory = false;

  PluginDelegate* plugin_delegate = ResourceHelper::GetPluginDelegate(this);
  if (!plugin_delegate)
    return PP_ERROR_FAILED;

  if (!plugin_delegate->RunFileChooser(params,
                                       new FileChooserCompletionImpl(this)))
    return PP_ERROR_FAILED;

  RegisterCallback(callback);
  return PP_OK_COMPLETIONPENDING;
}

PP_Resource PPB_FileChooser_Impl::GetNextChosenFile() {
  if (next_chosen_file_index_ >= chosen_files_.size())
    return 0;

  return chosen_files_[next_chosen_file_index_++]->GetReference();
}

std::vector<WebString> PPB_FileChooser_Impl::ParseAcceptValue(
    const std::string& accept_mime_types) {
  if (accept_mime_types.empty())
    return std::vector<WebString>();
  std::vector<std::string> mime_type_list;
  base::SplitString(accept_mime_types, ',', &mime_type_list);
  std::vector<WebString> normalized_mime_type_list;
  normalized_mime_type_list.reserve(mime_type_list.size());
  for (size_t i = 0; i < mime_type_list.size(); ++i) {
    std::string mime_type = mime_type_list[i];
    TrimWhitespaceASCII(mime_type, TRIM_ALL, &mime_type);
    if (mime_type.empty())
      continue;
    if (mime_type.find_first_of('/') == std::string::npos)
      continue;
    StringToLowerASCII(&mime_type);
    normalized_mime_type_list.push_back(WebString::fromUTF8(mime_type.data(),
                                                            mime_type.size()));
  }
  return normalized_mime_type_list;
}

}  // namespace ppapi
}  // namespace webkit
