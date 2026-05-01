module;
#include "waywallen/thumb/service.moc.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRunnable>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QStringBuilder>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>

#include <algorithm>
#include <utility>

module waywallen;

import :thumb.service;
import waywallen.ffmpeg;

namespace waywallen
{

namespace {

constexpr std::uint32_t kMaxEdge      = 512u;
constexpr int           kMaxThreads   = 4;

auto thumb_root() -> QString {
    if (auto v = qEnvironmentVariable("WAYWALLEN_THUMB_DIR"); ! v.isEmpty()) {
        return v;
    }
    if (auto v = qEnvironmentVariable("XDG_CACHE_HOME"); ! v.isEmpty()) {
        return v % QStringLiteral("/thumbnails");
    }
    return QDir::homePath() % QStringLiteral("/.cache/thumbnails");
}

void ensure_dir(const QString& path, QFile::Permissions perms) {
    QDir().mkpath(path);
    QFile::setPermissions(path, perms);
}

auto compute_cache_path(const QString& abs_path) -> QString {
    const QString root = thumb_root();
    const QString sub  = root % QStringLiteral("/x-large");
    constexpr QFile::Permissions dir_perms =
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
    ensure_dir(root, dir_perms);
    ensure_dir(sub, dir_perms);

    const QString uri  = QStringLiteral("file://") % abs_path;
    const QByteArray h = QCryptographicHash::hash(uri.toUtf8(),
                                                  QCryptographicHash::Md5).toHex();
    return sub % QStringLiteral("/") % QString::fromLatin1(h) %
           QStringLiteral(".png");
}

auto read_cache_mtime(const QString& cache_path) -> QString {
    QImageReader r(cache_path);
    return r.text(QStringLiteral("Thumb::MTime"));
}

auto fit_inside(QSize src, std::uint32_t max_edge) -> QSize {
    if (src.width() <= 0 || src.height() <= 0) return QSize();
    const int me = static_cast<int>(max_edge);
    if (src.width() <= me && src.height() <= me) return src;
    if (src.width() >= src.height()) {
        return QSize(me, std::max(1, src.height() * me / src.width()));
    }
    return QSize(std::max(1, src.width() * me / src.height()), me);
}

bool write_thumb_png(const QImage& img, const QString& cache_path,
                     const QString& uri, qint64 src_mtime, qint64 src_size,
                     QString& err_out) {
    QImage tagged = img;
    tagged.setText(QStringLiteral("Thumb::URI"), uri);
    tagged.setText(QStringLiteral("Thumb::MTime"), QString::number(src_mtime));
    tagged.setText(QStringLiteral("Thumb::Size"),  QString::number(src_size));

    const auto rnd  = QRandomGenerator::system()->generate();
    const QString tmp = cache_path
        % QStringLiteral(".tmp.")
        % QString::number(QCoreApplication::applicationPid())
        % QStringLiteral(".")
        % QString::number(rnd, 16);

    QImageWriter w(tmp, "png");
    if (! w.write(tagged)) {
        err_out = w.errorString();
        QFile::remove(tmp);
        return false;
    }
    QFile::setPermissions(tmp, QFile::ReadOwner | QFile::WriteOwner);

    // Replace atomically. QFile::rename does not overwrite on POSIX, so
    // remove the destination first if it exists.
    if (QFile::exists(cache_path)) QFile::remove(cache_path);
    if (! QFile::rename(tmp, cache_path)) {
        err_out = QStringLiteral("rename failed: ") % tmp
                  % QStringLiteral(" -> ") % cache_path;
        QFile::remove(tmp);
        return false;
    }
    return true;
}

class ThumbnailJob : public QRunnable {
public:
    ThumbnailJob(ThumbnailService* svc,
                 QString           key,
                 QString           cache_path,
                 bool              is_video,
                 qint64            src_mtime,
                 qint64            src_size)
        : m_svc(svc),
          m_key(std::move(key)),
          m_cache_path(std::move(cache_path)),
          m_is_video(is_video),
          m_src_mtime(src_mtime),
          m_src_size(src_size) {
        setAutoDelete(true);
    }

    void run() override {
        QImage  img;
        QString error;

        if (m_is_video) {
            ff::ThumbOptions opts;
            opts.max_edge = kMaxEdge;
            auto res = ff::extract_thumbnail(m_key.toStdString(), opts);
            if (res.is_err()) {
                error = QString::fromStdString(std::move(res).unwrap_err().message);
            } else {
                auto rgba = std::move(res).unwrap();
                // QImage takes ownership of nothing; copy() detaches
                // before the rgba buffer goes out of scope.
                img = QImage(rgba.data.data(),
                             static_cast<int>(rgba.width),
                             static_cast<int>(rgba.height),
                             static_cast<int>(rgba.stride),
                             QImage::Format_RGBA8888).copy();
            }
        } else {
            QImageReader reader(m_key);
            reader.setAutoTransform(true);
            const QSize target = fit_inside(reader.size(), kMaxEdge);
            if (target.isValid() && ! target.isEmpty()) {
                reader.setScaledSize(target);
            }
            img = reader.read();
            if (img.isNull()) {
                error = reader.errorString();
            }
        }

        int     out_state = ThumbnailRequest::Failed;
        QString out_path;
        if (! img.isNull()) {
            const QString uri = QStringLiteral("file://") % m_key;
            QString werr;
            if (write_thumb_png(img, m_cache_path, uri, m_src_mtime, m_src_size, werr)) {
                out_state = ThumbnailRequest::Ready;
                out_path  = m_cache_path;
            } else {
                error = werr;
            }
        }

        if (auto* svc = m_svc.data()) {
            QMetaObject::invokeMethod(svc, "onJobFinished", Qt::QueuedConnection,
                Q_ARG(QString, m_key),
                Q_ARG(int,     out_state),
                Q_ARG(QString, out_path),
                Q_ARG(QString, error));
        }
    }

private:
    QPointer<ThumbnailService> m_svc;
    QString                    m_key;
    QString                    m_cache_path;
    bool                       m_is_video;
    qint64                     m_src_mtime;
    qint64                     m_src_size;
};

} // namespace

// ---------------------------------------------------------------------------
// ThumbnailService
// ---------------------------------------------------------------------------

ThumbnailService::ThumbnailService(QObject* parent): QObject(parent) {
    m_pool.setMaxThreadCount(std::min(QThread::idealThreadCount(), kMaxThreads));
}

auto ThumbnailService::instance() -> ThumbnailService* {
    static ThumbnailService* the = new ThumbnailService(QCoreApplication::instance());
    return the;
}

auto ThumbnailService::create(QQmlEngine*, QJSEngine*) -> ThumbnailService* {
    auto* s = instance();
    QJSEngine::setObjectOwnership(s, QJSEngine::CppOwnership);
    return s;
}

void ThumbnailService::submit(ThumbnailRequest* req) {
    if (! req) return;

    const QString preview  = req->source();
    const QString resource = req->resource();
    const QString wp_type  = req->wpType();

    QString job_path;
    bool    is_video = false;
    if (! preview.isEmpty()) {
        job_path = QFileInfo(preview).absoluteFilePath();
        is_video = false;
    } else if (wp_type == QStringLiteral("video") && ! resource.isEmpty()) {
        job_path = QFileInfo(resource).absoluteFilePath();
        is_video = true;
    } else {
        // Caller already set Failed before calling submit; nothing to do.
        return;
    }

    QFileInfo fi(job_path);
    if (! fi.exists()) {
        QPointer<ThumbnailRequest> rp(req);
        QMetaObject::invokeMethod(this, [rp]() {
            if (auto* r = rp.data()) {
                r->_applyResult(ThumbnailRequest::Failed, QString(),
                                QStringLiteral("source file not found"));
            }
        }, Qt::QueuedConnection);
        return;
    }

    const QString cache_path = compute_cache_path(job_path);
    const qint64  src_mtime  = fi.lastModified().toSecsSinceEpoch();
    const qint64  src_size   = fi.size();

    if (QFileInfo::exists(cache_path)) {
        const QString stored = read_cache_mtime(cache_path);
        if (! stored.isEmpty() && stored.toLongLong() == src_mtime) {
            QPointer<ThumbnailRequest> rp(req);
            QMetaObject::invokeMethod(this, [rp, cache_path]() {
                if (auto* r = rp.data()) {
                    r->_applyResult(ThumbnailRequest::Ready, cache_path,
                                    QString());
                }
            }, Qt::QueuedConnection);
            return;
        }
    }

    auto it = m_pending.find(job_path);
    if (it == m_pending.end()) {
        Pending p;
        p.key        = job_path;
        p.cache_path = cache_path;
        p.subscribers.append(QPointer<ThumbnailRequest>(req));
        m_pending.insert(job_path, std::move(p));
        m_pool.start(new ThumbnailJob(this, job_path, cache_path,
                                      is_video, src_mtime, src_size));
    } else {
        it->subscribers.append(QPointer<ThumbnailRequest>(req));
    }
}

void ThumbnailService::cancel(ThumbnailRequest* req) {
    if (! req) return;
    QPointer<ThumbnailRequest> rp(req);
    for (auto& p : m_pending) {
        p.subscribers.removeAll(rp);
    }
}

void ThumbnailService::onJobFinished(const QString& key, int state,
                                     const QString& cache_path,
                                     const QString& error) {
    auto it = m_pending.find(key);
    if (it == m_pending.end()) return;
    auto subs = std::move(it->subscribers);
    m_pending.erase(it);

    for (auto& wp : subs) {
        if (auto* r = wp.data()) {
            r->_applyResult(static_cast<ThumbnailRequest::State>(state),
                            cache_path, error);
        }
    }
}

// ---------------------------------------------------------------------------
// ThumbnailRequest
// ---------------------------------------------------------------------------

ThumbnailRequest::ThumbnailRequest(QObject* parent): QObject(parent) {
    m_init_done = true;
}

ThumbnailRequest::~ThumbnailRequest() {
    if (auto* svc = ThumbnailService::instance()) {
        svc->cancel(this);
    }
}

void ThumbnailRequest::setSource(const QString& v) {
    if (m_source == v) return;
    m_source = v;
    Q_EMIT sourceChanged();
    scheduleSubmit();
}

void ThumbnailRequest::setResource(const QString& v) {
    if (m_resource == v) return;
    m_resource = v;
    Q_EMIT resourceChanged();
    scheduleSubmit();
}

void ThumbnailRequest::setWpType(const QString& v) {
    if (m_wp_type == v) return;
    m_wp_type = v;
    Q_EMIT wpTypeChanged();
    scheduleSubmit();
}

void ThumbnailRequest::scheduleSubmit() {
    if (! m_init_done) return;
    auto* svc = ThumbnailService::instance();
    svc->cancel(this);

    const bool has_preview        = ! m_source.isEmpty();
    const bool has_video_fallback =
        m_wp_type == QStringLiteral("video") && ! m_resource.isEmpty();

    if (! has_preview && ! has_video_fallback) {
        setCachePathInternal(QString());
        setErrorInternal(QStringLiteral("no preview source"));
        setStateInternal(Failed);
        return;
    }

    setCachePathInternal(QString());
    setErrorInternal(QString());
    setStateInternal(Loading);
    svc->submit(this);
}

void ThumbnailRequest::_applyResult(State state, QString cache_path,
                                    QString error) {
    setCachePathInternal(cache_path);
    setErrorInternal(error);
    setStateInternal(state);
}

void ThumbnailRequest::setStateInternal(State s) {
    if (m_state == s) return;
    m_state = s;
    Q_EMIT stateChanged();
}

void ThumbnailRequest::setCachePathInternal(const QString& p) {
    if (m_cache_path == p) return;
    m_cache_path = p;
    Q_EMIT cachePathChanged();
}

void ThumbnailRequest::setErrorInternal(const QString& e) {
    if (m_error == e) return;
    m_error = e;
    Q_EMIT errorChanged();
}

} // namespace waywallen

#include "waywallen/thumb/service.moc.cpp"
