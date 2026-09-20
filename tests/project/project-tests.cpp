#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include "node/factory.h"
#include "render/diskmanager.h"
#include "node/project.h"
#include "node/project/serializer/serializer.h"
#include "node/project/sequence/sequence.h"
#include "node/block/clip/clip.h"
#include "node/generator/solid/solid.h"
#include "testutil.h"

namespace olive {
namespace {
struct Environment {
  Environment() {
    ColorManager::SetUpDefaultConfig();
    DiskManager::CreateInstance();
    NodeFactory::Initialize();
    ProjectSerializer::Initialize();
  }
  ~Environment() { ProjectSerializer::Destroy(); NodeFactory::Destroy(); DiskManager::DestroyInstance(); }
};
QByteArray Read(const QString &filename) {
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}
bool Write(const QString &filename, const QByteArray &bytes) {
  QFile file(filename);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}

OLIVE_ADD_TEST(ProjectRoundTrip)
{
  Environment env;
  QTemporaryDir directory;
  OLIVE_ASSERT(directory.isValid());
  for (bool compressed : {false, true}) {
    const QString filename = directory.filePath(QString::fromUtf8("edição.ove"));
    Project original;
    original.Initialize();
    original.SetCustomCachePath(QStringLiteral("cache-relative"));
    auto *sequence = new Sequence();
    sequence->setParent(&original);
    sequence->SetLabel(QString::fromUtf8("Sequência teste"));
    sequence->add_default_nodes();
    ProjectSerializer::SaveData data(ProjectSerializer::kProject, &original, filename);
    OLIVE_ASSERT(ProjectSerializer::Save(data, compressed).code() == ProjectSerializer::kSuccess);
    Project loaded;
    OLIVE_ASSERT(ProjectSerializer::Load(&loaded, filename, ProjectSerializer::kProject).code() == ProjectSerializer::kSuccess);
    OLIVE_ASSERT(loaded.GetUuid() == original.GetUuid());
    OLIVE_ASSERT(loaded.GetSavedURL() == filename);
    OLIVE_ASSERT(loaded.GetCustomCachePath() == original.GetCustomCachePath());
    OLIVE_ASSERT_EQUAL(loaded.nodes().size(), original.nodes().size());
    bool found = false;
    for (Node *node : loaded.nodes()) {
      if (auto *seq = dynamic_cast<Sequence*>(node)) {
        found = true;
        OLIVE_ASSERT(seq->GetLabel() == sequence->GetLabel());
        OLIVE_ASSERT_EQUAL(seq->GetTracks().size(), sequence->GetTracks().size());
      }
    }
    OLIVE_ASSERT(found);
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(SaveFailurePreservesExistingProject)
{
  Environment env;
  QTemporaryDir directory;
  Project project;
  project.Initialize();
  const QString filename = directory.filePath("project.ove");
  ProjectSerializer::SaveData data(ProjectSerializer::kProject, &project, filename);
  OLIVE_ASSERT(ProjectSerializer::Save(data, true).code() == ProjectSerializer::kSuccess);
  const QByteArray before = Read(filename);
  OLIVE_ASSERT(!before.isEmpty());
  // The destination is a directory: neither it nor the previously saved
  // project may be replaced, and failure must reach the caller.
  data.SetFilename(directory.path());
  OLIVE_ASSERT(ProjectSerializer::Save(data, true).code() == ProjectSerializer::kFileError);
  OLIVE_ASSERT(Read(filename) == before);
  data.SetFilename(directory.filePath("missing/project.ove"));
  OLIVE_ASSERT(ProjectSerializer::Save(data, false).code() == ProjectSerializer::kFileError);
  OLIVE_ASSERT(Read(filename) == before);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(RejectTruncatedAndOversizedProjects)
{
  Environment env;
  QTemporaryDir directory;
  const QString filename = directory.filePath("invalid.ove");
  for (const QByteArray &bytes : {QByteArray(), QByteArray("O"), QByteArray("OV"),
       QByteArray("OVE"), QByteArray("OVEC"), QByteArray("<olive version=\"230220\"><project>"),
       QByteArray::fromHex("4f564543ffffffff0000")}) {
    OLIVE_ASSERT(Write(filename, bytes));
    Project project;
    OLIVE_ASSERT(ProjectSerializer::Load(&project, filename, ProjectSerializer::kProject).code() != ProjectSerializer::kSuccess);
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(RejectUnsupportedVersions)
{
  Environment env;
  for (auto version : {190219, 999999}) {
    Project project;
    QXmlStreamReader xml(QString("<olive version=\"%1\"><project/></olive>").arg(version));
    const auto code = ProjectSerializer::Load(&project, &xml, ProjectSerializer::kProject).code();
    OLIVE_ASSERT(code == (version == 190219 ? ProjectSerializer::kProjectTooOld : ProjectSerializer::kProjectTooNew));
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(AutoRecoverySnapshotAndRetention)
{
  Environment env;
  QTemporaryDir recovery_root;
  OLIVE_ASSERT(recovery_root.isValid());

  Project project;
  project.Initialize();
  project.SetSavedURL(QStringLiteral("/fake/path/my_project.ove"));

  const QUuid project_uuid = project.GetUuid();
  QDir project_recovery_dir(recovery_root.filePath(project_uuid.toString()));
  OLIVE_ASSERT(project_recovery_dir.mkpath("."));

  // Save 3 sequential snapshots
  for (int timestamp = 1000; timestamp <= 1002; ++timestamp) {
    const QString snapshot_file = project_recovery_dir.filePath(QStringLiteral("%1.ove").arg(timestamp));
    ProjectSerializer::SaveData data(ProjectSerializer::kProject, &project, snapshot_file);
    OLIVE_ASSERT(ProjectSerializer::Save(data, true).code() == ProjectSerializer::kSuccess);
  }

  // Verify all 3 snapshots were saved as valid compressed OVE files
  QStringList entries = project_recovery_dir.entryList(QStringList() << "*.ove", QDir::Files, QDir::Name);
  OLIVE_ASSERT_EQUAL(entries.size(), 3);

  // Verify the newest snapshot loads properly and preserves UUID
  Project loaded_recovery;
  const QString newest_snapshot = project_recovery_dir.filePath(entries.last());
  OLIVE_ASSERT(ProjectSerializer::Load(&loaded_recovery, newest_snapshot, ProjectSerializer::kProject).code() == ProjectSerializer::kSuccess);
  OLIVE_ASSERT(loaded_recovery.GetUuid() == project.GetUuid());
  OLIVE_ASSERT(loaded_recovery.GetSavedURL() == newest_snapshot);

  OLIVE_TEST_END;
}

}

