/*
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "telewhite/telewhite_message_details.h"

#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "telewhite/telewhite_mods.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Telewhite {
namespace {

[[nodiscard]] QString PeerIdText(not_null<PeerData*> peer) {
	const auto id = peer->id;
	if (peerIsUser(id)) {
		return QString::number(peerToUser(id).bare);
	} else if (peerIsChat(id)) {
		return u"-"_q + QString::number(peerToChat(id).bare);
	}
	return u"-100"_q + QString::number(peerToChannel(id).bare);
}

[[nodiscard]] QString PeerText(not_null<PeerData*> peer) {
	return peer->name() + u" ("_q + PeerIdText(peer) + u")"_q;
}

void AddLine(QStringList &lines, const QString &label, const QString &value) {
	if (!value.isEmpty()) {
		lines.push_back(label + u": "_q + value);
	}
}

void AddDocumentLines(QStringList &lines, not_null<DocumentData*> document) {
	AddLine(
		lines,
		tr::lng_telewhite_details_file(tr::now),
		document->filename());
	AddLine(
		lines,
		tr::lng_telewhite_details_type(tr::now),
		document->mimeString());
	if (document->size > 0) {
		AddLine(
			lines,
			tr::lng_telewhite_details_size(tr::now),
			Ui::FormatSizeText(document->size));
	}
	const auto size = document->dimensions;
	if (size.width() > 0 && size.height() > 0) {
		AddLine(
			lines,
			tr::lng_telewhite_details_dimensions(tr::now),
			QString::number(size.width())
				+ QChar(0x00D7)
				+ QString::number(size.height()));
	}
	const auto duration = document->duration();
	if (duration > 0) {
		AddLine(
			lines,
			tr::lng_telewhite_details_duration(tr::now),
			Ui::FormatDurationText(duration / 1000));
	}
	AddLine(
		lines,
		tr::lng_telewhite_details_media_id(tr::now),
		QString::number(document->id));
}

void AddPhotoLines(QStringList &lines, not_null<PhotoData*> photo) {
	if (photo->width() > 0 && photo->height() > 0) {
		AddLine(
			lines,
			tr::lng_telewhite_details_dimensions(tr::now),
			QString::number(photo->width())
				+ QChar(0x00D7)
				+ QString::number(photo->height()));
	}
	AddLine(
		lines,
		tr::lng_telewhite_details_media_id(tr::now),
		QString::number(photo->id));
}

[[nodiscard]] QString DetailsText(not_null<HistoryItem*> item) {
	auto lines = QStringList();
	AddLine(
		lines,
		tr::lng_telewhite_details_message_id(tr::now),
		QString::number(item->id.bare));
	AddLine(
		lines,
		tr::lng_telewhite_details_chat(tr::now),
		PeerText(item->history()->peer));
	AddLine(
		lines,
		tr::lng_telewhite_details_sender(tr::now),
		PeerText(item->from()));
	AddLine(
		lines,
		tr::lng_telewhite_details_date(tr::now),
		FormatExactDateTime(item->date()));
	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		if (edited->date) {
			AddLine(
				lines,
				tr::lng_telewhite_details_edited(tr::now),
				FormatExactDateTime(edited->date));
		}
	}
	if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
		const auto sender = forwarded->originalSender;
		AddLine(
			lines,
			tr::lng_telewhite_details_forward_from(tr::now),
			(sender
				? PeerText(sender)
				: !forwarded->originalPostAuthor.isEmpty()
				? forwarded->originalPostAuthor
				: tr::lng_telewhite_details_forward_hidden(tr::now)));
		if (forwarded->originalDate) {
			AddLine(
				lines,
				tr::lng_telewhite_details_forward_date(tr::now),
				FormatExactDateTime(forwarded->originalDate));
		}
		if (forwarded->originalId) {
			AddLine(
				lines,
				tr::lng_telewhite_details_forward_id(tr::now),
				QString::number(forwarded->originalId.bare));
		}
	}
	if (const auto views = item->viewsCount(); views > 0) {
		AddLine(
			lines,
			tr::lng_telewhite_details_views(tr::now),
			QString::number(views));
	}
	if (const auto replies = item->repliesCount(); replies > 0) {
		AddLine(
			lines,
			tr::lng_telewhite_details_replies(tr::now),
			QString::number(replies));
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			AddDocumentLines(lines, document);
		} else if (const auto photo = media->photo()) {
			AddPhotoLines(lines, photo);
		}
	}
	return lines.join(u"\n"_q);
}

void DetailsBox(not_null<Ui::GenericBox*> box, QString text) {
	box->setTitle(tr::lng_telewhite_details_title());
	box->setWidth(st::boxWideWidth);
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(text),
		st::boxLabel));
	label->setSelectable(true);
	box->addButton(tr::lng_telewhite_details_copy(), [=] {
		QGuiApplication::clipboard()->setText(text);
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void AddMessageDetailsAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!item->isRegular()) {
		return;
	}
	const auto text = DetailsText(item);
	menu->addAction(tr::lng_telewhite_details(tr::now), [=] {
		controller->show(Box(DetailsBox, text));
	}, &st::menuIconInfo);
}

} // namespace Telewhite
