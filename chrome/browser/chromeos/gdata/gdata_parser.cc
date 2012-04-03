// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/gdata/gdata_parser.h"

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/json/json_value_converter.h"
#include "base/memory/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/libxml_utils.h"

using base::Value;
using base::DictionaryValue;
using base::ListValue;

namespace gdata {

namespace {

// Term values for kSchemeKind category:
const char kSchemeKind[] = "http://schemas.google.com/g/2005#kind";
const char kTermPrefix[] = "http://schemas.google.com/docs/2007#";
const char kFileTerm[] = "file";
const char kFolderTerm[] = "folder";
const char kItemTerm[] = "item";
const char kPdfTerm[] = "pdf";
const char kDocumentTerm[] = "document";
const char kSpreadSheetTerm[] = "spreadsheet";
const char kPresentationTerm[] = "presentation";

const char kSchemeLabels[] = "http://schemas.google.com/g/2005/labels";

struct EntryKindMap {
  DocumentEntry::EntryKind kind;
  const char* entry;
  const char* extension;
};

const EntryKindMap kEntryKindMap[] = {
    { DocumentEntry::ITEM,         "item",         NULL},
    { DocumentEntry::DOCUMENT,     "document",     ".gdoc"},
    { DocumentEntry::SPREADSHEET,  "spreadsheet",  ".gsheet"},
    { DocumentEntry::PRESENTATION, "presentation", ".gslides" },
    { DocumentEntry::DRAWING,      "drawing",      ".gdraw"},
    { DocumentEntry::TABLE,        "table",        ".gtable"},
    { DocumentEntry::SITE,         "site",         NULL},
    { DocumentEntry::FOLDER,       "folder",       NULL},
    { DocumentEntry::FILE,         "file",         NULL},
    { DocumentEntry::PDF,          "pdf",          NULL},
};

struct LinkTypeMap {
  Link::LinkType type;
  const char* rel;
};

const LinkTypeMap kLinkTypeMap[] = {
    { Link::SELF,
      "self" },
    { Link::NEXT,
      "next" },
    { Link::PARENT,
      "http://schemas.google.com/docs/2007#parent" },
    { Link::ALTERNATE,
      "alternate"},
    { Link::EDIT,
      "edit" },
    { Link::EDIT_MEDIA,
      "edit-media" },
    { Link::ALT_EDIT_MEDIA,
      "http://schemas.google.com/docs/2007#alt-edit-media" },
    { Link::ALT_POST,
      "http://schemas.google.com/docs/2007#alt-post" },
    { Link::FEED,
      "http://schemas.google.com/g/2005#feed"},
    { Link::POST,
      "http://schemas.google.com/g/2005#post"},
    { Link::BATCH,
      "http://schemas.google.com/g/2005#batch"},
    { Link::THUMBNAIL,
      "http://schemas.google.com/docs/2007/thumbnail"},
    { Link::RESUMABLE_EDIT_MEDIA,
      "http://schemas.google.com/g/2005#resumable-edit-media"},
    { Link::RESUMABLE_CREATE_MEDIA,
      "http://schemas.google.com/g/2005#resumable-create-media"},
    { Link::TABLES_FEED,
      "http://schemas.google.com/spreadsheets/2006#tablesfeed"},
    { Link::WORKSHEET_FEED,
      "http://schemas.google.com/spreadsheets/2006#worksheetsfeed"},
    { Link::EMBED,
      "http://schemas.google.com/docs/2007#embed"},
    { Link::ICON,
      "http://schemas.google.com/docs/2007#icon"},
};

struct FeedLinkTypeMap {
  FeedLink::FeedLinkType type;
  const char* rel;
};

const FeedLinkTypeMap kFeedLinkTypeMap[] = {
    { FeedLink::ACL,
      "http://schemas.google.com/acl/2007#accessControlList" },
    { FeedLink::REVISIONS,
      "http://schemas.google.com/docs/2007/revisions" },
};

struct CategoryTypeMap {
  Category::CategoryType type;
  const char* scheme;
};

const CategoryTypeMap kCategoryTypeMap[] = {
    { Category::KIND,
      "http://schemas.google.com/g/2005#kind" },
    { Category::LABEL,
      "http://schemas.google.com/g/2005/labels" },
};

// Converts |url_string| to |result|.  Always returns true to be used
// for JSONValueConverter::RegisterCustomField method.
// TODO(mukai): make it return false in case of invalid |url_string|.
bool GetGURLFromString(const base::StringPiece& url_string, GURL* result) {
  *result = GURL(url_string.as_string());
  return true;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Author implementation

const char Author::kNameField[] = "name.$t";
const char Author::kEmailField[] = "email.$t";

const char Author::kAuthorNode[] = "author";
const char Author::kNameNode[] = "name";
const char Author::kEmailNode[] = "email";

Author::Author() {
}

// static
void Author::RegisterJSONConverter(
    base::JSONValueConverter<Author>* converter) {
  converter->RegisterStringField(kNameField, &Author::name_);
  converter->RegisterStringField(kEmailField, &Author::email_);
}

Author* Author::CreateFromXml(XmlReader* xml_reader) {
  if (xml_reader->NodeName() != kAuthorNode)
    return NULL;

  if (!xml_reader->Read())
    return NULL;

  const int depth = xml_reader->Depth();
  Author* author = new Author();
  bool skip_read = false;
  do {
    skip_read = false;
    DVLOG(1) << "Parsing author node " << xml_reader->NodeName()
            << ", depth = " << depth;
    if (xml_reader->NodeName() == kNameNode) {
     std::string name;
     if (xml_reader->ReadElementContent(&name))
       author->name_ = UTF8ToUTF16(name);
     skip_read = true;
    } else if (xml_reader->NodeName() == kEmailNode) {
     xml_reader->ReadElementContent(&author->email_);
     skip_read = true;
    }
  } while (depth == xml_reader->Depth() && (skip_read || xml_reader->Next()));
  return author;
}

////////////////////////////////////////////////////////////////////////////////
// Link implementation

const char Link::kHrefField[] = "href";
const char Link::kRelField[] = "rel";
const char Link::kTitleField[] = "title";
const char Link::kTypeField[] = "type";

const char Link::kLinkNode[] = "link";
const char Link::kHrefAttr[] = "href";
const char Link::kRelAttr[] = "rel";
const char Link::kTypeAttr[] = "type";

Link::Link() : type_(Link::UNKNOWN) {
}

// static.
bool Link::GetLinkType(const base::StringPiece& rel, Link::LinkType* result) {
  for (size_t i = 0; i < arraysize(kLinkTypeMap); i++) {
    if (rel == kLinkTypeMap[i].rel) {
      *result = kLinkTypeMap[i].type;
      return true;
    }
  }
  // Let unknown link types through, just report it; if the link type is needed
  // in the future, add it into LinkType and kLinkTypeMap.
  DVLOG(1) << "Ignoring unknown link type for rel " << rel;
  *result = UNKNOWN;
  return true;
}

// static
void Link::RegisterJSONConverter(base::JSONValueConverter<Link>* converter) {
  converter->RegisterCustomField<Link::LinkType>(
      kRelField, &Link::type_, &Link::GetLinkType);
  converter->RegisterCustomField(kHrefField, &Link::href_, &GetGURLFromString);
  converter->RegisterStringField(kTitleField, &Link::title_);
  converter->RegisterStringField(kTypeField, &Link::mime_type_);
}

// static.
Link* Link::CreateFromXml(XmlReader* xml_reader) {
  if (xml_reader->NodeName() != kLinkNode)
    return NULL;

  Link* link = new Link();
  xml_reader->NodeAttribute(kTypeAttr, &link->mime_type_);

  std::string href;
  if (xml_reader->NodeAttribute(kHrefAttr, &href))
      link->href_ = GURL(href);

  std::string rel;
  if (xml_reader->NodeAttribute(kRelAttr, &rel))
    GetLinkType(rel, &link->type_);

  return link;
}

////////////////////////////////////////////////////////////////////////////////
// FeedLink implementation

const char FeedLink::kHrefField[] = "href";
const char FeedLink::kRelField[] = "rel";

const char FeedLink::kFeedLinkNode[] = "feedLink";
const char FeedLink::kHrefAttr[] = "href";
const char FeedLink::kRelAttr[] = "rel";

FeedLink::FeedLink() : type_(FeedLink::UNKNOWN) {
}

// static.
bool FeedLink::GetFeedLinkType(
    const base::StringPiece& rel, FeedLink::FeedLinkType* result) {
  for (size_t i = 0; i < arraysize(kFeedLinkTypeMap); i++) {
    if (rel == kFeedLinkTypeMap[i].rel) {
      *result = kFeedLinkTypeMap[i].type;
      return true;
    }
  }
  DVLOG(1) << "Unknown feed link type for rel " << rel;
  return false;
}

// static
void FeedLink::RegisterJSONConverter(
    base::JSONValueConverter<FeedLink>* converter) {
  converter->RegisterCustomField<FeedLink::FeedLinkType>(
      kRelField, &FeedLink::type_, &FeedLink::GetFeedLinkType);
  converter->RegisterCustomField(
      kHrefField, &FeedLink::href_, &GetGURLFromString);
}

// static
FeedLink* FeedLink::CreateFromXml(XmlReader* xml_reader) {
  if (xml_reader->NodeName() != kFeedLinkNode)
    return NULL;

  FeedLink* link = new FeedLink();
  std::string href;
  if (xml_reader->NodeAttribute(kHrefAttr, &href))
    link->href_ = GURL(href);

  std::string rel;
  if (xml_reader->NodeAttribute(kRelAttr, &rel))
    GetFeedLinkType(rel, &link->type_);

  return link;
}

////////////////////////////////////////////////////////////////////////////////
// Category implementation

const char Category::kLabelField[] = "label";
const char Category::kSchemeField[] = "scheme";
const char Category::kTermField[] = "term";

const char Category::kCategoryNode[] = "category";
const char Category::kLabelAttr[] = "label";
const char Category::kSchemeAttr[] = "scheme";
const char Category::kTermAttr[] = "term";

Category::Category() : type_(UNKNOWN) {
}

// Converts category.scheme into CategoryType enum.
bool Category::GetCategoryTypeFromScheme(
    const base::StringPiece& scheme, Category::CategoryType* result) {
  for (size_t i = 0; i < arraysize(kCategoryTypeMap); i++) {
    if (scheme == kCategoryTypeMap[i].scheme) {
      *result = kCategoryTypeMap[i].type;
      return true;
    }
  }
  DVLOG(1) << "Unknown feed link type for scheme " << scheme;
  return false;
}

// static
void Category::RegisterJSONConverter(
    base::JSONValueConverter<Category>* converter) {
  converter->RegisterStringField(kLabelField, &Category::label_);
  converter->RegisterCustomField<Category::CategoryType>(
      kSchemeField, &Category::type_, &Category::GetCategoryTypeFromScheme);
  converter->RegisterStringField(kTermField, &Category::term_);
}

// static
Category* Category::CreateFromXml(XmlReader* xml_reader) {
  if (xml_reader->NodeName() != kCategoryNode)
    return NULL;

  Category* category = new Category();
  xml_reader->NodeAttribute(kTermAttr, &category->term_);

  std::string scheme;
  if (xml_reader->NodeAttribute(kSchemeAttr, &scheme))
    GetCategoryTypeFromScheme(scheme, &category->type_);

  std::string label;
  if (xml_reader->NodeAttribute(kLabelAttr, &label))
    category->label_ = UTF8ToUTF16(label);

  return category;
}

const Link* GDataEntry::GetLinkByType(Link::LinkType type) const {
  for (size_t i = 0; i < links_.size(); ++i) {
    if (links_[i]->type() == type)
      return links_[i];
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// Content implementation

const char Content::kSrcField[] = "src";
const char Content::kTypeField[] = "type";

const char Content::kContentNode[] = "content";
const char Content::kSrcAttr[] = "src";
const char Content::kTypeAttr[] = "type";

Content::Content() {
}

// static
void Content::RegisterJSONConverter(
    base::JSONValueConverter<Content>* converter) {
  converter->RegisterCustomField(kSrcField, &Content::url_, &GetGURLFromString);
  converter->RegisterStringField(kTypeField, &Content::mime_type_);
}

Content* Content::CreateFromXml(XmlReader* xml_reader) {
  if (xml_reader->NodeName() != kContentNode)
    return NULL;

  Content* content = new Content();
  std::string src;
  if (xml_reader->NodeAttribute(kSrcAttr, &src))
    content->url_ = GURL(src);

  xml_reader->NodeAttribute(kTypeAttr, &content->mime_type_);
  return content;
}

////////////////////////////////////////////////////////////////////////////////
// GDataEntry implementation

const char GDataEntry::kTimeParsingDelimiters[] = "-:.TZ";
const char GDataEntry::kAuthorField[] = "author";
const char GDataEntry::kLinkField[] = "link";
const char GDataEntry::kCategoryField[] = "category";
const char GDataEntry::kETagField[] = "gd$etag";
const char GDataEntry::kUpdatedField[] = "updated.$t";

GDataEntry::GDataEntry() {
}

GDataEntry::~GDataEntry() {
}

// static
void GDataEntry::RegisterJSONConverter(
    base::JSONValueConverter<GDataEntry>* converter) {
  converter->RegisterStringField(kETagField, &GDataEntry::etag_);
  converter->RegisterRepeatedMessage(kAuthorField, &GDataEntry::authors_);
  converter->RegisterRepeatedMessage(kLinkField, &GDataEntry::links_);
  converter->RegisterRepeatedMessage(kCategoryField, &GDataEntry::categories_);
  converter->RegisterCustomField<base::Time>(
      kUpdatedField,
      &GDataEntry::updated_time_,
      &GDataEntry::GetTimeFromString);
}

// static
bool GDataEntry::GetTimeFromString(const base::StringPiece& raw_value,
                                   base::Time* time) {
  std::vector<base::StringPiece> parts;
  if (Tokenize(raw_value, kTimeParsingDelimiters, &parts) != 7)
    return false;

  base::Time::Exploded exploded;
  if (!base::StringToInt(parts[0], &exploded.year) ||
      !base::StringToInt(parts[1], &exploded.month) ||
      !base::StringToInt(parts[2], &exploded.day_of_month) ||
      !base::StringToInt(parts[3], &exploded.hour) ||
      !base::StringToInt(parts[4], &exploded.minute) ||
      !base::StringToInt(parts[5], &exploded.second) ||
      !base::StringToInt(parts[6], &exploded.millisecond)) {
    return false;
  }

  exploded.day_of_week = 0;
  if (!exploded.HasValidValues())
    return false;

  *time = base::Time::FromLocalExploded(exploded);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
// DocumentEntry implementation

const char DocumentEntry::kFeedLinkField[] = "gd$feedLink";
const char DocumentEntry::kContentField[] = "content";
const char DocumentEntry::kFileNameField[] = "docs$filename.$t";
const char DocumentEntry::kMD5Field[] = "docs$md5Checksum.$t";
const char DocumentEntry::kSizeField[] = "docs$size.$t";
const char DocumentEntry::kSuggestedFileNameField[] =
    "docs$suggestedFilename.$t";
const char DocumentEntry::kResourceIdField[] = "gd$resourceId.$t";
const char DocumentEntry::kIDField[] = "id.$t";
const char DocumentEntry::kTitleField[] = "title.$t";
const char DocumentEntry::kPublishedField[] = "published.$t";

const char DocumentEntry::kEntryNode[] = "entry";
// Attributes are not namespace-blind as node names in XmlReader.
const char DocumentEntry::kETagAttr[] = "gd:etag";
const char DocumentEntry::kAuthorNode[] = "author";
const char DocumentEntry::kNameAttr[] = "name";
const char DocumentEntry::kEmailAttr[] = "email";
const char DocumentEntry::kUpdatedNode[] = "updated";

const char DocumentEntry::kIDNode[] = "id";
const char DocumentEntry::kPublishedNode[] = "published";
const char DocumentEntry::kEditedNode[] = "edited";

const char DocumentEntry::kTitleNode[] = "title";

const char DocumentEntry::kContentNode[] = "content";
const char DocumentEntry::kSrcAttr[] = "src";
const char DocumentEntry::kTypeAttr[] = "type";

const char DocumentEntry::kResourceIdNode[] = "resourceId";
const char DocumentEntry::kModifiedByMeDateNode[] = "modifiedByMeDate";
const char DocumentEntry::kLastModifiedByNode[] = "lastModifiedBy";
const char DocumentEntry::kQuotaBytesUsedNode[] = "quotaBytesUsed";

const char DocumentEntry::kWritersCanInviteNode[] = "writersCanInvite";
const char DocumentEntry::kValueAttr[] = "value";

const char DocumentEntry::kMd5ChecksumNode[] = "md5Checksum";
const char DocumentEntry::kFilenameNode[] = "filename";
const char DocumentEntry::kSuggestedFilenameNode[] = "suggestedFilename";
const char DocumentEntry::kSizeNode[] = "size";

DocumentEntry::DocumentEntry() : kind_(DocumentEntry::UNKNOWN), file_size_(0) {
}

DocumentEntry::~DocumentEntry() {
}

// static
void DocumentEntry::RegisterJSONConverter(
    base::JSONValueConverter<DocumentEntry>* converter) {
  // inheritant the parent registrations.
  GDataEntry::RegisterJSONConverter(
      reinterpret_cast<base::JSONValueConverter<GDataEntry>*>(converter));
  converter->RegisterStringField(
      kResourceIdField, &DocumentEntry::resource_id_);
  converter->RegisterStringField(kIDField, &DocumentEntry::id_);
  converter->RegisterStringField(kTitleField, &DocumentEntry::title_);
  converter->RegisterCustomField<base::Time>(
      kPublishedField, &DocumentEntry::published_time_,
      &GDataEntry::GetTimeFromString);
  converter->RegisterRepeatedMessage(
      kFeedLinkField, &DocumentEntry::feed_links_);
  converter->RegisterNestedField(kContentField, &DocumentEntry::content_);

  // File properties.  If the document type is not a normal file, then
  // that's no problem because those feed must not have these fields
  // themselves, which does not report errors.
  converter->RegisterStringField(kFileNameField, &DocumentEntry::filename_);
  converter->RegisterStringField(kMD5Field, &DocumentEntry::file_md5_);
  converter->RegisterCustomField<int64>(
      kSizeField, &DocumentEntry::file_size_, &base::StringToInt64);
  converter->RegisterStringField(
      kSuggestedFileNameField, &DocumentEntry::suggested_filename_);
}

std::string DocumentEntry::GetHostedDocumentExtension() const {
  for (size_t i = 0; i < arraysize(kEntryKindMap); i++) {
    if (kEntryKindMap[i].kind == kind_) {
      if (kEntryKindMap[i].extension)
        return std::string(kEntryKindMap[i].extension);
      else
        return std::string();
    }
  }
  return std::string();
}

// static
bool DocumentEntry::HasHostedDocumentExtension(const FilePath& file) {
  FilePath::StringType file_extension = file.Extension();
  for (size_t i = 0; i < arraysize(kEntryKindMap); ++i) {
    const char* document_extension = kEntryKindMap[i].extension;
    if (document_extension && file_extension == document_extension)
      return true;
  }
  return false;
}

// static
DocumentEntry::EntryKind DocumentEntry::GetEntryKindFromTerm(
    const std::string& term) {
  if (!StartsWithASCII(term, kTermPrefix, false)) {
    DVLOG(1) << "Unexpected term prefix term " << term;
    return DocumentEntry::UNKNOWN;
  }

  std::string type = term.substr(strlen(kTermPrefix));
  for (size_t i = 0; i < arraysize(kEntryKindMap); i++) {
    if (type == kEntryKindMap[i].entry)
      return kEntryKindMap[i].kind;
  }
  DVLOG(1) << "Unknown entry type for term " << term << ", type " << type;
  return DocumentEntry::UNKNOWN;
}

void DocumentEntry::FillRemainingFields() {
  // Set |kind_| and |labels_| based on the |categories_| in the class.
  // JSONValueConverter does not have the ability to catch an element in a list
  // based on a predicate.  Thus we need to iterate over |categories_| and
  // find the elements to set these fields as a post-process.
  for (size_t i = 0; i < categories_.size(); ++i) {
    const Category* category = categories_[i];
    if (category->type() == Category::KIND)
      kind_ = GetEntryKindFromTerm(category->term());
    else if (category->type() == Category::LABEL)
      labels_.push_back(category->label());
  }
}

// static
DocumentEntry* DocumentEntry::CreateFrom(const base::Value* value) {
  base::JSONValueConverter<DocumentEntry> converter;
  scoped_ptr<DocumentEntry> entry(new DocumentEntry());
  if (!converter.Convert(*value, entry.get())) {
    DVLOG(1) << "Invalid document entry!";
    return NULL;
  }

  entry->FillRemainingFields();
  return entry.release();
}

// static.
DocumentEntry* DocumentEntry::CreateFromXml(XmlReader* xml_reader) {
  if (xml_reader->NodeName() != kEntryNode)
    return NULL;

  DocumentEntry* entry = new DocumentEntry();
  xml_reader->NodeAttribute(kETagAttr, &entry->etag_);

  if (!xml_reader->Read())
    return entry;

  bool skip_read = false;
  do {
    DVLOG(1) << "Parsing node " << xml_reader->NodeName();
    skip_read = false;

    if (xml_reader->NodeName() == Author::kAuthorNode) {
      scoped_ptr<Author> author(Author::CreateFromXml(xml_reader));
      if (author.get())
        entry->authors_.push_back(author.release());
    }

    if (xml_reader->NodeName() == Content::kContentNode) {
      scoped_ptr<Content> content(Content::CreateFromXml(xml_reader));
      if (content.get())
        entry->content_ = *content.get();
    } else if (xml_reader->NodeName() == Link::kLinkNode) {
      scoped_ptr<Link> link(Link::CreateFromXml(xml_reader));
      if (link.get())
        entry->links_.push_back(link.release());
    } else if (xml_reader->NodeName() == FeedLink::kFeedLinkNode) {
      scoped_ptr<FeedLink> link(FeedLink::CreateFromXml(xml_reader));
      if (link.get())
        entry->feed_links_.push_back(link.release());
    } else if (xml_reader->NodeName() == Category::kCategoryNode) {
      scoped_ptr<Category> category(Category::CreateFromXml(xml_reader));
      if (category.get())
        entry->categories_.push_back(category.release());
    } else if (xml_reader->NodeName() == kUpdatedNode) {
      std::string time;
      if (xml_reader->ReadElementContent(&time))
        GetTimeFromString(time, &entry->updated_time_);
      skip_read = true;
    } else if (xml_reader->NodeName() == kPublishedNode) {
      std::string time;
      if (xml_reader->ReadElementContent(&time))
        GetTimeFromString(time, &entry->published_time_);
      skip_read = true;
    } else if (xml_reader->NodeName() == kIDNode) {
      xml_reader->ReadElementContent(&entry->id_);
      skip_read = true;
    } else if (xml_reader->NodeName() == kResourceIdNode) {
      xml_reader->ReadElementContent(&entry->resource_id_);
      skip_read = true;
    } else if (xml_reader->NodeName() == kTitleNode) {
      std::string title;
      if (xml_reader->ReadElementContent(&title))
        entry->title_ = UTF8ToUTF16(title);
      skip_read = true;
    } else if (xml_reader->NodeName() == kFilenameNode) {
      std::string file_name;
      if (xml_reader->ReadElementContent(&file_name))
        entry->filename_ = UTF8ToUTF16(file_name);
      skip_read = true;
    } else if (xml_reader->NodeName() == kSuggestedFilenameNode) {
      std::string suggested_filename;
      if (xml_reader->ReadElementContent(&suggested_filename))
        entry->suggested_filename_ = UTF8ToUTF16(suggested_filename);
      skip_read = true;
    } else if (xml_reader->NodeName() == kMd5ChecksumNode) {
      xml_reader->ReadElementContent(&entry->file_md5_);
      skip_read = true;
    } else if (xml_reader->NodeName() == kSizeNode) {
      std::string size;
      if (xml_reader->ReadElementContent(&size))
        base::StringToInt64(size, &entry->file_size_);
      skip_read = true;
    } else {
      DVLOG(1) << "Unknown node " << xml_reader->NodeName();
    }
  } while (skip_read || xml_reader->Next());

  entry->FillRemainingFields();
  return entry;
}

////////////////////////////////////////////////////////////////////////////////
// DocumentFeed implementation

const char DocumentFeed::kStartIndexField[] = "openSearch$startIndex.$t";
const char DocumentFeed::kItemsPerPageField[] =
    "openSearch$itemsPerPage.$t";
const char DocumentFeed::kTitleField[] = "title.$t";
const char DocumentFeed::kEntryField[] = "entry";

DocumentFeed::DocumentFeed() : start_index_(0), items_per_page_(0) {
}

DocumentFeed::~DocumentFeed() {
}

// static
void DocumentFeed::RegisterJSONConverter(
    base::JSONValueConverter<DocumentFeed>* converter) {
  // inheritance
  GDataEntry::RegisterJSONConverter(
      reinterpret_cast<base::JSONValueConverter<GDataEntry>*>(converter));
  // TODO(zelidrag): Once we figure out where these will be used, we should
  // check for valid start_index_ and items_per_page_ values.
  converter->RegisterCustomField<int>(
      kStartIndexField, &DocumentFeed::start_index_, &base::StringToInt);
  converter->RegisterCustomField<int>(
      kItemsPerPageField, &DocumentFeed::items_per_page_, &base::StringToInt);
  converter->RegisterStringField(kTitleField, &DocumentFeed::title_);
  converter->RegisterRepeatedMessage(kEntryField, &DocumentFeed::entries_);
}

bool DocumentFeed::Parse(base::Value* value) {
  base::JSONValueConverter<DocumentFeed> converter;
  if (!converter.Convert(*value, this)) {
    DVLOG(1) << "Invalid document feed!";
    return false;
  }

  for (size_t i = 0; i < entries_.size(); ++i) {
    entries_[i]->FillRemainingFields();
  }
  return true;
}

// static
DocumentFeed* DocumentFeed::CreateFrom(base::Value* value) {
  scoped_ptr<DocumentFeed> feed(new DocumentFeed());
  if (!feed->Parse(value)) {
    DVLOG(1) << "Invalid document feed!";
    return NULL;
  }

  return feed.release();
}

bool DocumentFeed::GetNextFeedURL(GURL* url) {
  DCHECK(url);
  for (size_t i = 0; i < links_.size(); ++i) {
    if (links_[i]->type() == Link::NEXT) {
      *url = links_[i]->href();
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// AccountMetadataFeed implementation

const char AccountMetadataFeed::kQuotaBytesTotalField[] =
    "entry.gd$quotaBytesTotal.$t";
const char AccountMetadataFeed::kQuotaBytesUsedField[] =
    "entry.gd$quotaBytesUsed.$t";
const char AccountMetadataFeed::kLargestChangestampField[] =
    "entry.docs$largestChangestamp.value";

AccountMetadataFeed::AccountMetadataFeed()
    : quota_bytes_total_(0),
      quota_bytes_used_(0),
      largest_changestamp_(0) {
}

AccountMetadataFeed::~AccountMetadataFeed() {
}

// static
void AccountMetadataFeed::RegisterJSONConverter(
    base::JSONValueConverter<AccountMetadataFeed>* converter) {
  converter->RegisterCustomField<int>(kQuotaBytesTotalField,
                                      &AccountMetadataFeed::quota_bytes_total_,
                                      &base::StringToInt);
  converter->RegisterCustomField<int>(kQuotaBytesUsedField,
                                      &AccountMetadataFeed::quota_bytes_used_,
                                      &base::StringToInt);
  converter->RegisterCustomField<int>(
      kLargestChangestampField,
      &AccountMetadataFeed::largest_changestamp_,
      &base::StringToInt);
}

// static
AccountMetadataFeed* AccountMetadataFeed::CreateFrom(base::Value* value) {
  scoped_ptr<AccountMetadataFeed> feed(new AccountMetadataFeed());
  if (!feed->Parse(value)) {
    LOG(ERROR) << "Unable to create: Invalid account metadata feed!";
    return NULL;
  }

  return feed.release();
}

bool AccountMetadataFeed::Parse(base::Value* value) {
  base::JSONValueConverter<AccountMetadataFeed> converter;
  if (!converter.Convert(*value, this)) {
    LOG(ERROR) << "Unable to parse: Invalid account metadata feed!";
    return false;
  }
  return true;
}

}  // namespace gdata
