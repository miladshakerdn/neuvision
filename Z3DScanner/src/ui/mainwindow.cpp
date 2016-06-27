#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <Z3DCalibratedCamera>
#include <Z3DCameraAcquisition>


#include "zpatternprojection.h"
#include "zpatternprojectionprovider.h"
#include "zstructuredlightsystem.h"
#include "zstructuredlightsystemprovider.h"

#include "src/zpointcloudwidget.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>





MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentPatternProjection(nullptr)
    , m_currentStructuredLightSystem(nullptr)
{
    ui->setupUi(this);

    m_pointCloudWidget = new ZPointCloudWidget(this);
    ui->glLayout->addWidget(m_pointCloudWidget);

    /// disable all until ready
    ui->centralwidget->setEnabled(false);

    /// finish initialization
    QTimer::singleShot(100, this, &MainWindow::init);
}

MainWindow::~MainWindow()
{
    qDebug() << "deleting pattern projection instances...";
    foreach(Z3D::ZPatternProjection *patternProjection, m_patternProjectionList) {
        delete patternProjection;
    }

    qDebug() << "deleting ui...";
    delete ui;

    qDebug() << Q_FUNC_INFO << "finished";
}

void MainWindow::init()
{
    /// connect combobox to slot
    connect(ui->structuredLightSystemComboBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(onStructuredLightSystemTypeChanged(int)));

    /// add structured light systems
    m_structuredLightList << Z3D::ZStructuredLightSystemProvider::getAll();
    for (const auto *structuredLightSystem : m_structuredLightList) {
        ui->structuredLightSystemComboBox->addItem(structuredLightSystem->displayName());
    }

    /// connect combobox to slot
    connect(ui->patternTypeComboBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(onPatternProjectionTypeChanged(int)));

    /// add types of pattern projection
    m_patternProjectionList << Z3D::ZPatternProjectionProvider::getAll();
    for (const auto *patternProjection : m_patternProjectionList) {
        ui->patternTypeComboBox->addItem(patternProjection->displayName());
    }
}

void MainWindow::closeEvent(QCloseEvent * /*event*/)
{
    /// closing the main window causes the program to quit
    qApp->quit();
}

void MainWindow::onPatternProjectionTypeChanged(int index)
{
    /// remove previous config widget and hide it
    if (m_currentPatternProjection) {
        QWidget *previousWidget = m_currentPatternProjection->configWidget();
        previousWidget->setVisible(false);
        ui->patternProjectionConfigLayout->removeWidget(previousWidget);
    }

    /// update current pattern finder
    m_currentPatternProjection = m_patternProjectionList[index];
    m_currentStructuredLightSystem->setPatternProjection(m_currentPatternProjection);

    /// add config widget
    QWidget *currentWidget = m_currentPatternProjection->configWidget();
    ui->patternProjectionConfigLayout->addWidget(currentWidget);
    currentWidget->setVisible(true);
}

void MainWindow::onStructuredLightSystemTypeChanged(int index)
{
    /// remove previous config widget and hide it
    if (m_currentStructuredLightSystem) {
        QWidget *previousWidget = m_currentStructuredLightSystem->configWidget();
        previousWidget->setVisible(false);
        ui->structuredLightSystemConfigLayout->removeWidget(previousWidget);

        disconnect(m_currentStructuredLightSystem, &Z3D::ZStructuredLightSystem::readyChanged,
                   ui->centralwidget, &QWidget::setEnabled);
        disconnect(m_currentStructuredLightSystem, &Z3D::ZStructuredLightSystem::scanFinished,
                   this, &MainWindow::onScanFinished);
        disconnect(ui->startAcquisitionButton, &QPushButton::clicked,
                   m_currentStructuredLightSystem, &Z3D::ZStructuredLightSystem::start);
    }

    /// update current structured light system
    m_currentStructuredLightSystem = m_structuredLightList[index];
    m_currentStructuredLightSystem->setPatternProjection(m_currentPatternProjection);

    connect(m_currentStructuredLightSystem, &Z3D::ZStructuredLightSystem::readyChanged,
            ui->centralwidget, &QWidget::setEnabled);
    connect(m_currentStructuredLightSystem, &Z3D::ZStructuredLightSystem::scanFinished,
            this, &MainWindow::onScanFinished);
    connect(ui->startAcquisitionButton, &QPushButton::clicked,
            m_currentStructuredLightSystem, &Z3D::ZStructuredLightSystem::start);

    QDir configDir = QDir::current();
#if defined(Q_OS_WIN)
//        if (pluginsDir.dirName().toLower() == "debug" || pluginsDir.dirName().toLower() == "release")
//            pluginsDir.cdUp();
#elif defined(Q_OS_MAC)
    if (configDir.dirName() == "MacOS") {
        configDir.cdUp();
        configDir.cdUp();
        configDir.cdUp();
    }
#endif

    QString settingsFile = configDir.absoluteFilePath(QString("%1.ini").arg(QApplication::applicationName()));
    qDebug() << "trying to load config from:" << settingsFile;
    QSettings settings(settingsFile, QSettings::IniFormat);
    m_currentStructuredLightSystem->init(&settings);

    /// add config widget
    QWidget *currentWidget = m_currentStructuredLightSystem->configWidget();
    ui->structuredLightSystemConfigLayout->addWidget(currentWidget);
    currentWidget->setVisible(true);
}

void MainWindow::onScanFinished(Z3D::ZSimplePointCloud::Ptr cloud)
{
    if (!cloud) {
        QMessageBox::warning(this, tr("Scan error"),
                             tr("The point cloud is empty.\nIs everything configured correctly?\nIs there anything inside the scan volume?"));
        return;
    }

    m_pointCloudWidget->setPointCloudData(ZPointCloudData::Ptr(new ZPointCloudData(cloud)));

    /*
    QString filename = QString("%1/pointCloud.pcd").arg(decodedPattern->scanTmpFolder);
    qDebug() << "saving point cloud data to" << filename;
    pcl::io::savePCDFile(qPrintable(filename), *cloud);
    */
    /// FIXME
    /// getCloudViewer()->addPointCloud(cloud);


    QDir folder = QDir::current();
#if defined(Q_OS_MAC)
    if (folder.dirName() == "MacOS") {
        folder.cdUp();
        folder.cdUp();
        folder.cdUp();
    }
#endif

    QString fileName = QFileDialog::getSaveFileName(
                this,
                tr("Save point cloud"),
                QString("%1/%2_pointCloud.asc")
                    .arg(folder.absolutePath())
                    .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss")),
                tr("ASCII file (*.asc);;Point Cloud Library file (*.pcd)"));

    if (fileName.isEmpty() || fileName.isNull()) {
        return;
    }

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream fileTextStream(&file);
        if (fileName.endsWith(".pcd", Qt::CaseInsensitive)) {
            /// if trying to save to PCD format
            fileTextStream << QString(
                              "# .PCD v.7 - Point Cloud Data file format\n"
                              "VERSION .7\n"
                              "FIELDS x y z rgb\n"
                              "SIZE 4 4 4 4\n"
                              "TYPE F F F F\n"
                              "COUNT 1 1 1 1\n"
                              "WIDTH %1\n"
                              "HEIGHT 1\n"
                              "VIEWPOINT 0 0 0 1 0 0 0\n"
                              "POINTS %1\n"
                              "DATA ascii\n")
                              .arg(cloud->points.size());
            for (const auto &point : cloud->points) {
                fileTextStream << point[0] << " " << point[1] << " " << point[2] << " " << point[3] << "\n";
            }
        } else if (fileName.endsWith(".asc", Qt::CaseInsensitive)) {
            /// ASCII file format
            for (const auto &point : cloud->points) {
                fileTextStream << point[0] << " " << point[1] << " " << point[2] << " " << point[3] << "\n";
            }
        } else {
            QMessageBox::warning(this, tr("Error saving point cloud"),
                                 tr("Invalid file %1\nNothing saved.").arg(fileName));
        }
        fileTextStream.flush();
        file.flush();
        file.close();
        qDebug() << "point cloud written to" << file.fileName();
    } else {
        qCritical() << "cant open file to write fringePoints" << file.fileName();
    }
}
