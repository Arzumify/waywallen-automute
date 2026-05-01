module;
#include "QExtra/macro_qt.hpp"

#ifdef Q_MOC_RUN
#    include "waywallen/thumb/service.moc"
#endif

// QThreadPool is not exported by qextra's `qt` module; bring it in
// via the global module fragment. Other Qt types (QObject, QString,
// QHash, QPointer, QList, QQmlEngine, QJSEngine) come from `import qextra`.
#include <QtCore/QThreadPool>

export module waywallen:thumb.service;
export import qextra;

namespace waywallen
{

export class ThumbnailRequest;

/// Background thumbnail generator. Resolves cache hits from
/// `$XDG_CACHE_HOME/thumbnails/x-large/` per the freedesktop Thumbnail
/// Managing Standard, and dispatches misses to a `QThreadPool` for
/// QImageReader / waywallen::ff decode + atomic PNG write.
///
/// QML-singleton; per-card requests are `ThumbnailRequest` objects that
/// register themselves with the service on each input change.
export class ThumbnailService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static auto    instance() -> ThumbnailService*;
    static auto    create(QQmlEngine*, QJSEngine*) -> ThumbnailService*;

    /// Submit a request. Service stores a `QPointer` to `req` and
    /// invokes back via queued connection on completion. Calling
    /// `submit` again with the same `req` after its source/wpType
    /// changes is the supported way to re-issue.
    void submit(ThumbnailRequest* req);
    /// Drop any pending subscription for `req` (e.g. on destruction).
    void cancel(ThumbnailRequest* req);

private:
    explicit ThumbnailService(QObject* parent = nullptr);

    struct Pending {
        QString                           key;        // absolute job-input path
        QString                           cache_path; // resolved x-large cache path
        QList<QPointer<ThumbnailRequest>> subscribers;
    };

    QThreadPool             m_pool;
    QHash<QString, Pending> m_pending; // key = absolute job_path

    Q_INVOKABLE void onJobFinished(const QString& key,
                                   int            state,
                                   const QString& cache_path,
                                   const QString& error);
};

/// Per-card request handle. QML hosts one of these inside
/// `ThumbnailImage.qml`; on `source` / `resource` / `wpType` change it
/// re-submits to `ThumbnailService` and updates `state` / `cachePath`
/// from the worker's result.
export class ThumbnailRequest : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString source     READ source    WRITE setSource    NOTIFY sourceChanged    FINAL)
    Q_PROPERTY(QString resource   READ resource  WRITE setResource  NOTIFY resourceChanged  FINAL)
    Q_PROPERTY(QString wpType     READ wpType    WRITE setWpType    NOTIFY wpTypeChanged    FINAL)
    Q_PROPERTY(State   state      READ state                        NOTIFY stateChanged     FINAL)
    Q_PROPERTY(QString cachePath  READ cachePath                    NOTIFY cachePathChanged FINAL)
    Q_PROPERTY(QString error      READ error                        NOTIFY errorChanged     FINAL)

public:
    enum State { Idle, Loading, Ready, Failed };
    Q_ENUM(State)

    explicit ThumbnailRequest(QObject* parent = nullptr);
    ~ThumbnailRequest() override;

    auto source() const -> const QString& { return m_source; }
    void setSource(const QString& v);

    auto resource() const -> const QString& { return m_resource; }
    void setResource(const QString& v);

    auto wpType() const -> const QString& { return m_wp_type; }
    void setWpType(const QString& v);

    auto state() const -> State { return m_state; }
    auto cachePath() const -> const QString& { return m_cache_path; }
    auto error() const -> const QString& { return m_error; }

    // Service callback (gui thread).
    void _applyResult(State state, QString cache_path, QString error);

Q_SIGNALS:
    void sourceChanged();
    void resourceChanged();
    void wpTypeChanged();
    void stateChanged();
    void cachePathChanged();
    void errorChanged();

private:
    void scheduleSubmit();
    void setStateInternal(State s);
    void setCachePathInternal(const QString& p);
    void setErrorInternal(const QString& e);

    QString m_source;
    QString m_resource;
    QString m_wp_type;
    State   m_state { Idle };
    QString m_cache_path;
    QString m_error;
    bool    m_init_done { false };
};

} // namespace waywallen
