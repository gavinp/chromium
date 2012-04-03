// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/template_url_parser.h"

#include <algorithm>
#include <map>
#include <vector>

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "libxml/parser.h"
#include "libxml/xmlwriter.h"
#include "ui/gfx/favicon_size.h"

namespace {

// NOTE: libxml uses the UTF-8 encoding. As 0-127 of UTF-8 corresponds
// to that of char, the following names are all in terms of char. This avoids
// having to convert to wide, then do comparisons.

// Defines for element names of the OSD document:
const char kURLElement[] = "Url";
const char kParamElement[] = "Param";
const char kShortNameElement[] = "ShortName";
const char kImageElement[] = "Image";
const char kOpenSearchDescriptionElement[] = "OpenSearchDescription";
const char kFirefoxSearchDescriptionElement[] = "SearchPlugin";
const char kInputEncodingElement[] = "InputEncoding";

// Various XML attributes used.
const char kURLTypeAttribute[] = "type";
const char kURLTemplateAttribute[] = "template";
const char kImageTypeAttribute[] = "type";
const char kImageWidthAttribute[] = "width";
const char kImageHeightAttribute[] = "height";
const char kParamNameAttribute[] = "name";
const char kParamValueAttribute[] = "value";
const char kParamMethodAttribute[] = "method";

// Mime type for search results.
const char kHTMLType[] = "text/html";

// Mime type for as you type suggestions.
const char kSuggestionType[] = "application/x-suggestions+json";

// Namespace identifier.
const char kOSDNS[] = "xmlns";

// The namespace for documents we understand.
const char kNameSpace[] = "http://a9.com/-/spec/opensearch/1.1/";

std::string XMLCharToString(const xmlChar* value) {
  return std::string(reinterpret_cast<const char*>(value));
}

// Returns true if input_encoding contains a valid input encoding string. This
// doesn't verify that we have a valid encoding for the string, just that the
// string contains characters that constitute a valid input encoding.
bool IsValidEncodingString(const std::string& input_encoding) {
  if (input_encoding.empty())
    return false;

  if (!IsAsciiAlpha(input_encoding[0]))
    return false;

  for (size_t i = 1, max = input_encoding.size(); i < max; ++i) {
    char c = input_encoding[i];
    if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != '.' && c != '_' &&
        c != '-') {
      return false;
    }
  }
  return true;
}

void AppendParamToQuery(const std::string& key,
                        const std::string& value,
                        std::string* query) {
  if (!query->empty())
    query->append("&");
  if (!key.empty()) {
    query->append(key);
    query->append("=");
  }
  query->append(value);
}

// Returns true if the ref is null, or the url wrapped by ref is
// valid with a spec of http/https.
bool IsHTTPRef(const TemplateURLRef* ref) {
  if (ref == NULL)
    return true;
  GURL url(ref->url());
  return (url.is_valid() && (url.SchemeIs(chrome::kHttpScheme) ||
                             url.SchemeIs(chrome::kHttpsScheme)));
}

}  // namespace


// TemplateURLParsingContext --------------------------------------------------

// To minimize memory overhead while parsing, a SAX style parser is used.
// TemplateURLParsingContext is used to maintain the state we're in the document
// while parsing.
class TemplateURLParsingContext {
 public:
  // Enum of the known element types.
  enum ElementType {
    UNKNOWN,
    OPEN_SEARCH_DESCRIPTION,
    URL,
    PARAM,
    SHORT_NAME,
    IMAGE,
    INPUT_ENCODING,
  };

  enum Method {
    GET,
    POST
  };

  // Key/value of a Param node.
  typedef std::pair<std::string, std::string> Param;

  explicit TemplateURLParsingContext(
      TemplateURLParser::ParameterFilter* parameter_filter);

  static void StartElementImpl(void* ctx,
                               const xmlChar* name,
                               const xmlChar** atts);
  static void EndElementImpl(void* ctx, const xmlChar* name);
  static void CharactersImpl(void* ctx, const xmlChar* ch, int len);

  // Returns a heap-allocated TemplateURL representing the result of parsing.
  // This will be NULL if parsing failed or if the results were invalid for some
  // reason (e.g. the resulting URL was not HTTP[S], a name wasn't supplied,
  // etc.).
  TemplateURL* GetTemplateURL(Profile* profile);

 private:
  // Key is UTF8 encoded.
  typedef std::map<std::string, ElementType> ElementNameToElementTypeMap;

  static void InitMapping();

  void ParseURL(const xmlChar** atts);
  void ParseImage(const xmlChar** atts);
  void ParseParam(const xmlChar** atts);
  void ProcessURLParams();

  // Returns the current ElementType.
  ElementType GetKnownType();

  static ElementNameToElementTypeMap* kElementNameToElementTypeMap;

  scoped_ptr<TemplateURL> url_;

  std::vector<ElementType> elements_;
  bool image_is_valid_for_favicon_;

  // Character content for the current element.
  string16 string_;

  TemplateURLParser::ParameterFilter* parameter_filter_;

  // The list of parameters parsed in the Param nodes of a Url node.
  std::vector<Param> extra_params_;

  // The HTTP methods used.
  Method method_;
  Method suggestion_method_;

  // If true, we are currently parsing a suggest URL, otherwise it is an HTML
  // search.  Note that we don't need a stack as URL nodes cannot be nested.
  bool is_suggest_url_;

  // Whether we should derive the image from the URL (when images are data
  // URLs).
  bool derive_image_from_url_;

  DISALLOW_COPY_AND_ASSIGN(TemplateURLParsingContext);
};

// static
TemplateURLParsingContext::ElementNameToElementTypeMap*
    TemplateURLParsingContext::kElementNameToElementTypeMap = NULL;

TemplateURLParsingContext::TemplateURLParsingContext(
    TemplateURLParser::ParameterFilter* parameter_filter)
    : url_(new TemplateURL()),
      image_is_valid_for_favicon_(false),
      parameter_filter_(parameter_filter),
      method_(GET),
      suggestion_method_(GET),
      is_suggest_url_(false),
      derive_image_from_url_(false) {
  if (kElementNameToElementTypeMap == NULL)
    InitMapping();
  // When combined with proscriptions elsewhere against updating url_->url_ to
  // the empty string, this call ensures url_->url() will never be NULL.
  url_->SetURL("x");
}

// static
void TemplateURLParsingContext::StartElementImpl(void* ctx,
                                                 const xmlChar* name,
                                                 const xmlChar** atts) {
  // Remove the namespace from |name|, ex: os:Url -> Url.
  std::string node_name(XMLCharToString(name));
  size_t index = node_name.find_first_of(":");
  if (index != std::string::npos)
    node_name.erase(0, index + 1);

  TemplateURLParsingContext* context =
      reinterpret_cast<TemplateURLParsingContext*>(ctx);
  context->elements_.push_back(
    context->kElementNameToElementTypeMap->count(node_name) ?
        (*context->kElementNameToElementTypeMap)[node_name] : UNKNOWN);
  switch (context->GetKnownType()) {
    case TemplateURLParsingContext::URL:
      context->extra_params_.clear();
      context->ParseURL(atts);
      break;
    case TemplateURLParsingContext::IMAGE:
      context->ParseImage(atts);
      break;
    case TemplateURLParsingContext::PARAM:
      context->ParseParam(atts);
      break;
    default:
      break;
  }
  context->string_.clear();
}

// static
void TemplateURLParsingContext::EndElementImpl(void* ctx, const xmlChar* name) {
  TemplateURLParsingContext* context =
      reinterpret_cast<TemplateURLParsingContext*>(ctx);
  switch (context->GetKnownType()) {
    case TemplateURLParsingContext::SHORT_NAME:
      context->url_->short_name_ = context->string_;
      break;
    case TemplateURLParsingContext::IMAGE: {
      GURL image_url(UTF16ToUTF8(context->string_));
      if (image_url.SchemeIs(chrome::kDataScheme)) {
        // TODO (jcampan): bug 1169256: when dealing with data URL, we need to
        // decode the data URL in the renderer. For now, we'll just point to the
        // favicon from the URL.
        context->derive_image_from_url_ = true;
      } else if (context->image_is_valid_for_favicon_ && image_url.is_valid() &&
                 (image_url.SchemeIs(chrome::kHttpScheme) ||
                  image_url.SchemeIs(chrome::kHttpsScheme))) {
        context->url_->set_favicon_url(image_url);
      }
      context->image_is_valid_for_favicon_ = false;
      break;
    }
    case TemplateURLParsingContext::INPUT_ENCODING: {
      std::string input_encoding = UTF16ToASCII(context->string_);
      if (IsValidEncodingString(input_encoding))
        context->url_->input_encodings_.push_back(input_encoding);
      break;
    }
    case TemplateURLParsingContext::URL:
      context->ProcessURLParams();
      break;
    default:
      break;
  }
  context->string_.clear();
  context->elements_.pop_back();
}

// static
void TemplateURLParsingContext::CharactersImpl(void* ctx,
                                               const xmlChar* ch,
                                               int len) {
  reinterpret_cast<TemplateURLParsingContext*>(ctx)->string_ +=
      UTF8ToUTF16(std::string(reinterpret_cast<const char*>(ch), len));
}

TemplateURL* TemplateURLParsingContext::GetTemplateURL(Profile* profile) {
  // Basic legality checks.
  if (url_->short_name_.empty() || !IsHTTPRef(url_->url()) ||
      !IsHTTPRef(url_->suggestions_url()))
    return NULL;

  // If the image was a data URL, use the favicon from the search URL instead.
  // (see TODO inEndElementImpl()).
  GURL url(url_->url()->url());
  if (derive_image_from_url_ && url_->favicon_url().is_empty())
    url_->set_favicon_url(TemplateURL::GenerateFaviconURL(url));

  // TODO(jcampan): http://b/issue?id=1196285 we do not support search engines
  //                that use POST yet.
  if (method_ == TemplateURLParsingContext::POST)
    return NULL;
  if (suggestion_method_ == TemplateURLParsingContext::POST)
    url_->SetSuggestionsURL(std::string());

  // Give this a keyword to facilitate tab-to-search.
  string16 keyword(TemplateURLService::GenerateKeyword(url, false));
  DCHECK(!keyword.empty());
  url_->set_keyword(keyword);
  return url_.release();
}

// static
void TemplateURLParsingContext::InitMapping() {
  kElementNameToElementTypeMap = new std::map<std::string, ElementType>;
  (*kElementNameToElementTypeMap)[kURLElement] = URL;
  (*kElementNameToElementTypeMap)[kParamElement] = PARAM;
  (*kElementNameToElementTypeMap)[kShortNameElement] = SHORT_NAME;
  (*kElementNameToElementTypeMap)[kImageElement] = IMAGE;
  (*kElementNameToElementTypeMap)[kOpenSearchDescriptionElement] =
      OPEN_SEARCH_DESCRIPTION;
  (*kElementNameToElementTypeMap)[kFirefoxSearchDescriptionElement] =
      OPEN_SEARCH_DESCRIPTION;
  (*kElementNameToElementTypeMap)[kInputEncodingElement] = INPUT_ENCODING;
}

void TemplateURLParsingContext::ParseURL(const xmlChar** atts) {
  if (!atts)
    return;

  std::string template_url;
  bool is_post = false;
  bool is_html_url = false;
  bool is_suggest_url = false;
  for (; *atts; atts += 2) {
    std::string name(XMLCharToString(*atts));
    const xmlChar* value = atts[1];
    if (name == kURLTypeAttribute) {
      std::string type = XMLCharToString(value);
      is_html_url = (type == kHTMLType);
      is_suggest_url = (type == kSuggestionType);
    } else if (name == kURLTemplateAttribute) {
      template_url = XMLCharToString(value);
    } else if (name == kParamMethodAttribute) {
      is_post = LowerCaseEqualsASCII(XMLCharToString(value), "post");
    }
  }

  if (is_html_url && !template_url.empty()) {
    url_->SetURL(template_url);
    is_suggest_url_ = false;
    if (is_post)
      method_ = POST;
  } else if (is_suggest_url) {
    url_->SetSuggestionsURL(template_url);
    is_suggest_url_ = true;
    if (is_post)
      suggestion_method_ = POST;
  }
}

void TemplateURLParsingContext::ParseImage(const xmlChar** atts) {
  if (!atts)
    return;

  int width = 0;
  int height = 0;
  std::string type;
  for (; *atts; atts += 2) {
    std::string name(XMLCharToString(*atts));
    const xmlChar* value = atts[1];
    if (name == kImageTypeAttribute) {
      type = XMLCharToString(value);
    } else if (name == kImageWidthAttribute) {
      base::StringToInt(XMLCharToString(value), &width);
    } else if (name == kImageHeightAttribute) {
      base::StringToInt(XMLCharToString(value), &height);
    }
  }

  image_is_valid_for_favicon_ = (width == gfx::kFaviconSize) &&
      (height == gfx::kFaviconSize) &&
      ((type == "image/x-icon") || (type == "image/vnd.microsoft.icon"));
}

void TemplateURLParsingContext::ParseParam(const xmlChar** atts) {
  if (!atts)
    return;

  std::string key, value;
  for (; *atts; atts += 2) {
    std::string name(XMLCharToString(*atts));
    const xmlChar* val = atts[1];
    if (name == kParamNameAttribute) {
      key = XMLCharToString(val);
    } else if (name == kParamValueAttribute) {
      value = XMLCharToString(val);
    }
  }

  if (!key.empty() &&
      (!parameter_filter_ || parameter_filter_->KeepParameter(key, value)))
    extra_params_.push_back(Param(key, value));
}

void TemplateURLParsingContext::ProcessURLParams() {
  if (!parameter_filter_ && extra_params_.empty())
    return;

  const TemplateURLRef* t_url_ref =
      is_suggest_url_ ? url_->suggestions_url() : url_->url();
  if (!t_url_ref)
    return;
  GURL url(t_url_ref->url());
  // If there is a parameter filter, parse the existing URL and remove any
  // unwanted parameter.
  std::string new_query;
  bool modified = false;
  if (parameter_filter_) {
    url_parse::Component query = url.parsed_for_possibly_invalid_spec().query;
    url_parse::Component key, value;
    const char* url_spec = url.spec().c_str();
    while (url_parse::ExtractQueryKeyValue(url_spec, &query, &key, &value)) {
      std::string key_str(url_spec, key.begin, key.len);
      std::string value_str(url_spec, value.begin, value.len);
      if (parameter_filter_->KeepParameter(key_str, value_str)) {
        AppendParamToQuery(key_str, value_str, &new_query);
      } else {
        modified = true;
      }
    }
  }
  if (!modified)
    new_query = url.query();

  // Add the extra parameters if any.
  if (!extra_params_.empty()) {
    modified = true;
    for (std::vector<Param>::const_iterator iter(extra_params_.begin());
         iter != extra_params_.end(); ++iter)
      AppendParamToQuery(iter->first, iter->second, &new_query);
  }

  if (modified) {
    GURL::Replacements repl;
    repl.SetQueryStr(new_query);
    url = url.ReplaceComponents(repl);
    if (is_suggest_url_)
      url_->SetSuggestionsURL(url.spec());
    else if (url.is_valid())
      url_->SetURL(url.spec());
  }
}

TemplateURLParsingContext::ElementType
    TemplateURLParsingContext::GetKnownType() {
  if (elements_.size() == 2 && elements_[0] == OPEN_SEARCH_DESCRIPTION)
    return elements_[1];
  // We only expect PARAM nodes under the URL node.
  return (elements_.size() == 3 && elements_[0] == OPEN_SEARCH_DESCRIPTION &&
      elements_[1] == URL && elements_[2] == PARAM) ? PARAM : UNKNOWN;
}


// TemplateURLParser ----------------------------------------------------------

// static
TemplateURL* TemplateURLParser::Parse(
    Profile* profile,
    const char* data,
    size_t length,
    TemplateURLParser::ParameterFilter* param_filter) {
  // xmlSubstituteEntitiesDefault(1) makes it so that &amp; isn't mapped to
  // &#38; . Unfortunately xmlSubstituteEntitiesDefault affects global state.
  // If this becomes problematic we'll need to provide our own entity
  // type for &amp;, or strip out &#38; by hand after parsing.
  int last_sub_entities_value = xmlSubstituteEntitiesDefault(1);
  TemplateURLParsingContext context(param_filter);
  xmlSAXHandler sax_handler;
  memset(&sax_handler, 0, sizeof(sax_handler));
  sax_handler.startElement = &TemplateURLParsingContext::StartElementImpl;
  sax_handler.endElement = &TemplateURLParsingContext::EndElementImpl;
  sax_handler.characters = &TemplateURLParsingContext::CharactersImpl;
  xmlSAXUserParseMemory(&sax_handler, &context, data, static_cast<int>(length));
  xmlSubstituteEntitiesDefault(last_sub_entities_value);

  return context.GetTemplateURL(profile);
}
