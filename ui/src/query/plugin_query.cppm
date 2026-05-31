module;
#include "QExtra/macro_qt.hpp"

#ifdef Q_MOC_RUN
#    include "waywallen/query/plugin_query.moc"
#endif

export module waywallen:query.plugin;
export import :query.query;

namespace waywallen
{

// Plugin-centric (package) view: one entry per installable plugin, with the
// renderer components it provides. `plugins` is a list of maps:
//   { id, name, version, hasSource, renderers: [{name, types, version, settings}] }
export class PluginListQuery : public Query,
                               public QueryExtra<control::v1::Response, PluginListQuery> {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged FINAL)

public:
    PluginListQuery(QObject* parent = nullptr);

    auto plugins() const -> const QVariantList&;

    void reload() override;

    Q_SIGNAL void pluginsChanged();

private:
    QVariantList m_plugins;
};

export class PluginInstallQuery : public Query,
                                  public QueryExtra<control::v1::Response, PluginInstallQuery> {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString zipPath READ zipPath WRITE setZipPath NOTIFY zipPathChanged FINAL)
    Q_PROPERTY(QString pluginId READ pluginId NOTIFY resultChanged FINAL)
    Q_PROPERTY(bool needsRestart READ needsRestart NOTIFY resultChanged FINAL)

public:
    PluginInstallQuery(QObject* parent = nullptr);

    auto zipPath() const -> const QString&;
    void setZipPath(const QString&);
    auto pluginId() const -> const QString&;
    auto needsRestart() const -> bool;

    void reload() override;

    Q_SIGNAL void zipPathChanged();
    Q_SIGNAL void resultChanged();
    Q_SIGNAL void installed(const QString& pluginId, bool needsRestart);

private:
    QString m_zip_path;
    QString m_plugin_id;
    bool    m_needs_restart = false;
};

} // namespace waywallen
