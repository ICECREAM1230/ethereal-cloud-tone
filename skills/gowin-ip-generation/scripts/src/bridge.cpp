#define QT_NO_VERSION_TAGGING
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QTreeWidgetItemIterator>
#include <QtWidgets/QDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtCore/QTimer>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMetaMethod>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>

static QJsonObject job, result;
static QPointer<QWidget> mainwin, coreview, dialog, protocolParent;
static bool parentFinalizing=false;
static QTimer *timer=nullptr;
static int phase=0, ticks=0;
static qint64 started=0, outputSince=0;
static QString base;
static void save(){QFile f(base+"/bridge.json");if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(result).toJson());f.close();}}
static void finish(QString status, QString why, int code){result["status"]=status;result["message"]=why;result["phase"]=phase;result["elapsed_ms"]=QDateTime::currentMSecsSinceEpoch()-started;save();fprintf(stderr,"BRIDGE_RESULT %s %s\n",status.toUtf8().constData(),why.toUtf8().constData());fflush(nullptr);_exit(code);}
static void stage(QString name){result["stage"]=name;save();fprintf(stderr,"BRIDGE_STAGE %s\n",name.toUtf8().constData());fflush(stderr);}
static void *handle(QString name){return dlopen((QString("/home/ice/Gowin/V1.9.12.04/IDE/plugins/ide/")+name).toUtf8().constData(),RTLD_LAZY|RTLD_NOLOAD);}
static QJsonArray fields(QWidget *w){
 QJsonArray rows;
 for(auto c:w->findChildren<QWidget*>()){
  if(!(qobject_cast<QLineEdit*>(c)||qobject_cast<QCheckBox*>(c)||qobject_cast<QComboBox*>(c)||qobject_cast<QSpinBox*>(c)||qobject_cast<QDoubleSpinBox*>(c)))continue;
  QJsonObject x; x["class"]=c->metaObject()->className();x["object"]=c->objectName();x["enabled"]=c->isEnabled();x["visible"]=c->isVisible();
  QStringList parents;for(QObject*p=c->parent();p&&p!=w;p=p->parent())parents.prepend(p->objectName());x["parents"]=parents.join('/');x["key"]=parents.isEmpty()?c->objectName():parents.join('/')+'/'+c->objectName();for(auto lab:w->findChildren<QLabel*>())if(lab->buddy()==c)x["label"]=lab->text();
  if(auto e=qobject_cast<QLineEdit*>(c)){x["value"]=e->text();x["read_only"]=e->isReadOnly();}
  if(auto e=qobject_cast<QCheckBox*>(c)){x["value"]=e->isChecked();x["label"]=e->text();}
  if(auto e=qobject_cast<QComboBox*>(c)){x["value"]=e->currentText();x["index"]=e->currentIndex();QJsonArray v;for(int i=0;i<e->count();i++)v.append(e->itemText(i));x["choices"]=v;}
  if(auto e=qobject_cast<QSpinBox*>(c)){x["value"]=e->value();x["min"]=e->minimum();x["max"]=e->maximum();}
  if(auto e=qobject_cast<QDoubleSpinBox*>(c)){x["value"]=e->value();x["min"]=e->minimum();x["max"]=e->maximum();}
  rows.append(x);
 }
 return rows;
}
static QJsonArray surfaces(){QJsonArray a;for(auto w:QApplication::topLevelWidgets())if(w->isVisible()&&w!=mainwin&&w!=coreview){QJsonObject x;x["class"]=w->metaObject()->className();x["title"]=w->windowTitle();x["fields"]=fields(w);QJsonArray m;for(int i=0;i<w->metaObject()->methodCount();i++)m.append(QString(w->metaObject()->method(i).methodSignature()));x["methods"]=m;a.append(x);}return a;}
static bool inGeneral(QWidget *w){for(QObject*p=w;p;p=p->parent()){if(auto g=qobject_cast<QGroupBox*>(p)){if(g->title().contains("General",Qt::CaseInsensitive))return true;}}return false;}
static bool setField(QWidget *c,QJsonValue v){
 if(auto e=qobject_cast<QCheckBox*>(c)){if(!v.isBool())return false;e->setChecked(v.toBool());return e->isChecked()==v.toBool();}
 if(auto e=qobject_cast<QComboBox*>(c)){int i=v.isDouble()?v.toInt():e->findText(v.toString());if(i<0||i>=e->count())return false;e->setCurrentIndex(i);return e->currentIndex()==i;}
 if(auto e=qobject_cast<QSpinBox*>(c)){if(!v.isDouble())return false;int n=v.toInt();if(n<e->minimum()||n>e->maximum())return false;e->setValue(n);return e->value()==n;}
 if(auto e=qobject_cast<QDoubleSpinBox*>(c)){if(!v.isDouble())return false;double n=v.toDouble();if(n<e->minimum()||n>e->maximum())return false;e->setValue(n);return e->value()==n;}
 if(auto e=qobject_cast<QLineEdit*>(c)){if(!v.isString()||e->isReadOnly())return false;e->setText(v.toString());return e->text()==v.toString();}return false;
}
static bool parameters(QWidget*w){
 QJsonObject applied;auto settings=job["set"].toObject();
 QStringList order;for(auto value:job["set_order"].toArray())order.append(value.toString());for(auto it=settings.begin();it!=settings.end();++it)if(!order.contains(it.key()))order.append(it.key());
 for(const auto &key:order){if(!settings.contains(key))continue;if(QStringList({"editCreateIn","editModuleName","editFileName","lineEditModuleName","lineEditFileName"}).contains(key.section('/',-1))){result["bad_parameter"]=key;return false;}QList<QWidget*> matches;for(auto c:w->findChildren<QWidget*>()){QStringList parents;for(QObject*p=c->parent();p&&p!=w;p=p->parent())parents.prepend(p->objectName());QString full=parents.isEmpty()?c->objectName():parents.join('/')+'/'+c->objectName();if((key==c->objectName()||key==full)&&(qobject_cast<QLineEdit*>(c)||qobject_cast<QComboBox*>(c)||qobject_cast<QCheckBox*>(c)||qobject_cast<QSpinBox*>(c)||qobject_cast<QDoubleSpinBox*>(c)))matches.append(c);}if(matches.size()!=1||!matches[0]->isEnabled()||!setField(matches[0],settings[key])){result["bad_parameter"]=key;result["applied_parameters"]=applied;result["fields_requested"]=fields(w);return false;}applied[key]=settings[key];}

 if(job["auto_parameter"].toBool()&&settings.isEmpty()){
  for(auto c:w->findChildren<QCheckBox*>())if(!inGeneral(c)&&c->isVisible()&&c->isEnabled()&&!c->objectName().isEmpty()){bool old=c->isChecked();c->setChecked(!old);if(c->isChecked()!=old){applied[c->objectName()]=c->isChecked();break;}}
  if(applied.isEmpty())for(auto c:w->findChildren<QComboBox*>())if(!inGeneral(c)&&c->isVisible()&&c->isEnabled()&&c->count()>1&&!c->objectName().isEmpty()){int old=c->currentIndex(),next=(old+1)%c->count();c->setCurrentIndex(next);if(c->currentIndex()!=old){applied[c->objectName()]=c->currentText();break;}}
  if(applied.isEmpty())for(auto c:w->findChildren<QSpinBox*>())if(!inGeneral(c)&&c->isVisible()&&c->isEnabled()&&c->minimum()<c->maximum()&&!c->objectName().isEmpty()){int old=c->value(),next=old<c->maximum()?old+1:old-1;c->setValue(next);if(c->value()!=old){applied[c->objectName()]=c->value();break;}}
 }
 result["applied_parameters"]=applied;
 return !job["auto_parameter"].toBool()||!applied.isEmpty();
}
static void tick(){
 if(++ticks>job["timeout_ticks"].toInt(600))finish("TIMEOUT","No completed generation within deadline",124);
 // Keep messages as evidence; never silently confirm overwrites or license prompts.
 for(auto w:QApplication::topLevelWidgets())if(auto m=qobject_cast<QMessageBox*>(w)){if(m->isVisible()){
  QString msg=m->text();result["dialog_text"]=msg;
  if(msg.contains("add generated file",Qt::CaseInsensitive)&&msg.contains("project",Qt::CaseInsensitive)){m->done(QMessageBox::Yes);}
  else finish("GENERATOR_MESSAGE",msg,12);
 }}
 if(phase==0){
  for(auto w:QApplication::topLevelWidgets())if(QString(w->metaObject()->className())=="Core::IdeMainWindow"){mainwin=w;break;}
  if(!mainwin)return;
  void*h=handle("libIdeCore.so");using Fn=void(*)(void*,const QString&);auto f=(Fn)dlsym(h,"_ZN4Core13IdeMainWindow8openFileERK7QString");if(!f)finish("INTERFACE_MISSING","IdeMainWindow::openFile not exported",10);
  phase=1;stage("open_isolated_project");f(mainwin.data(),job["project"].toString());return;
 }
 if(phase==1){
  void*h=handle("libCoreGen.so");if(!h)return;using F=QObject*(*)();auto f=(F)dlsym(h,"qt_plugin_instance");if(!f)finish("INTERFACE_MISSING","CoreGen plugin instance unavailable",10);QObject*p=f();phase=2;stage("invoke_CoreGen_onStartCoreGen");if(!QMetaObject::invokeMethod(p,"onStartCoreGen",Qt::QueuedConnection))finish("NOT_INVOKABLE","onStartCoreGen is not a Qt callable method",10);return;
 }
 if(phase==2){
  for(auto w:QApplication::allWidgets())if(QString(w->metaObject()->className())=="CoreGen::CoreGenView"){coreview=w;break;}
  if(!coreview)return;
  auto trees=coreview->findChildren<QTreeWidget*>();if(trees.isEmpty())return;QTreeWidgetItem*found=nullptr;QTreeWidget*tree=nullptr;int count=0;QJsonArray catalog;int matchCount=0;
  for(auto t:trees)for(QTreeWidgetItemIterator it(t);*it;++it){count++;QStringList parents;for(auto p=(*it)->parent();p;p=p->parent())parents.prepend(p->text(0));QJsonObject item;item["label"]=(*it)->text(0);item["version"]=(*it)->text(1);item["category"]=parents.join('|');item["hidden"]=(*it)->isHidden();QJsonObject roles;for(int role=Qt::UserRole;role<Qt::UserRole+12;role++){QVariant value=(*it)->data(0,role);if(value.isValid()&&value.userType()!=QMetaType::VoidStar)roles[QString::number(role)]=QJsonValue::fromVariant(value);}item["roles"]=roles;catalog.append(item);if((job["labels"].toArray().contains((*it)->text(0))||(*it)->text(0)==job["label"].toString())&&!(*it)->text(1).isEmpty()&&!(*it)->isHidden()&&(job["category"].toString().isEmpty()||parents.join('|')==job["category"].toString())){found=*it;tree=t;matchCount++;}}
  result["catalog_item_count"]=count;result["catalog"]=catalog;if(job["mode"].toString()=="catalog"&&count>20)finish("CATALOG_READY","Catalog collected without generation",0);if(matchCount>1)finish("AMBIGUOUS_CATALOG_ENTRY","More than one matching entry; refuse wrong IP",15);
  if(!found){if(count>20&&ticks>100)finish("NOT_AVAILABLE_FOR_TARGET","IP not present as selectable catalog entry for chosen device",11);return;}
  tree->setCurrentItem(found);result["selected_label"]=found->text(0);result["selected_version"]=found->text(1);phase=3;stage("invoke_IPCoreCategoryTree_onItemDbClicked");
  QModelIndex index=tree->currentIndex();if(!QMetaObject::invokeMethod(tree,"onItemDbClicked",Qt::QueuedConnection,Q_ARG(QModelIndex,index)))finish("NOT_INVOKABLE","onItemDbClicked is not Qt callable",10);return;
 }
 if(phase==3||phase==5){
  for(auto w:QApplication::topLevelWidgets())if(w->isVisible()&&!(phase==5&&QString(w->metaObject()->className())=="IPSERDESIP::serdesipdlg")&&(w->inherits("IPCore::IPGenDialog")||w->inherits("IPCore::ModuleGenDialog")||(qobject_cast<QDialog*>(w)&&w->findChild<QLineEdit*>("editCreateIn")))){dialog=w;break;}
  if(!dialog){if(ticks%10==0){result["visible_surfaces"]=surfaces();save();}return;}
  if(phase==3&&!job["protocol"].toString().isEmpty()){result["parent_dialog_class"]=dialog->metaObject()->className();protocolParent=dialog;for(QString name:{QString("lineEditModuleName"),QString("lineEditFileName")})if(auto e=dialog->findChild<QLineEdit*>(name))e->setText(job["module"].toString());if(auto e=dialog->findChild<QLineEdit*>("editCreateIn"))e->setText(job["output"].toString());phase=5;stage("open_serdes_protocol");if(!QMetaObject::invokeMethod(dialog,"createIP",Qt::QueuedConnection,Q_ARG(QString,job["protocol"].toString())))finish("INTERFACE_MISSING","SerDes createIP(QString) unavailable",10);dialog.clear();return;}
  result["dialog_class"]=dialog->metaObject()->className();result["fields_before"]=fields(dialog);QJsonArray methods;for(int i=0;i<dialog->metaObject()->methodCount();i++)methods.append(QString(dialog->metaObject()->method(i).methodSignature()));result["methods"]=methods;if(job["mode"].toString()=="inspect"){bool okay=parameters(dialog);result["fields_requested"]=fields(dialog);if(!okay)finish("PARAMETER_UNAVAILABLE","Inspect preview could not apply requested controls",13);finish("INSPECTED","Live parameter schema collected without generation",0);}
  void*h=handle("libCoreGen.so");using F=void(*)(void*,const QString&);bool module=dialog->inherits("IPCore::ModuleGenDialog");QString prefix=module?"_ZN6IPCore15ModuleGenDialog":"_ZN6IPCore11IPGenDialog";
  auto call=[&](QString name,QString value){auto f=(F)dlsym(h,(prefix+name).toUtf8().constData());if(!f)finish("INTERFACE_MISSING",name,10);f(dialog.data(),value);};
  bool standard=dialog->inherits("IPCore::IPGenDialog")||module;result["module_policy"]=standard?"configurable":"vendor_fixed";if(!standard){for(QString name:{QString("editModuleName"),QString("lineEditModuleName")})if(auto e=dialog->findChild<QLineEdit*>(name)){if(!e->isReadOnly()){e->setText(job["module"].toString()+(phase==5?"_lane":""));result["module_policy"]="configurable";}}for(QString name:{QString("editFileName"),QString("lineEditFileName")})if(auto e=dialog->findChild<QLineEdit*>(name))if(!e->isReadOnly())e->setText(job["module"].toString());}if(standard){call("15uiSetModuleNameERK7QString",job["module"].toString());call("13uiSetFileNameERK7QString",job["module"].toString());call("12setCreatPathERK7QString",job["output"].toString());}
  if(auto pathEdit=dialog->findChild<QLineEdit*>("editCreateIn"))pathEdit->setText(job["output"].toString());
  using PathFn=QString(*)(void*);QString getter=module?"_ZNK6IPCore15ModuleGenDialog10createPathEv":"_ZNK6IPCore11IPGenDialog10createPathEv";auto getPath=(PathFn)dlsym(h,getter.toUtf8().constData());if((standard&&(!getPath||QDir::cleanPath(getPath(dialog.data()))!=QDir::cleanPath(job["output"].toString())))||(!standard&&(!dialog->findChild<QLineEdit*>("editCreateIn")||QDir::cleanPath(dialog->findChild<QLineEdit*>("editCreateIn")->text())!=QDir::cleanPath(job["output"].toString()))))finish("OUTPUT_PATH_MISMATCH","Plugin did not accept exact isolated output path",14);
  if(!parameters(dialog))finish("PARAMETER_UNAVAILABLE","Requested parameter could not be set or no editable parameter found",13);
  result["fields_requested"]=fields(dialog);if(auto e=dialog->findChild<QLineEdit*>("editCreateIn"))if(QDir::cleanPath(e->text())!=QDir::cleanPath(job["output"].toString()))finish("OUTPUT_PATH_MISMATCH","Parameters redirected generation outside the isolated output",14);auto finalSettings=job["set"].toObject();for(auto it=finalSettings.begin();it!=finalSettings.end();++it){for(auto field:result["fields_requested"].toArray()){auto f=field.toObject();if(f["object"].toString()!=it.key()&&f["key"].toString()!=it.key())continue;QJsonValue actual=(f["class"].toString()=="QComboBox"&&it.value().isDouble())?f["index"]:f["value"];if(actual!=it.value()){result["bad_parameter"]=it.key();result["requested_value"]=it.value();result["actual_value"]=actual;finish("PARAMETER_NOT_RETAINED","Dependent parameter update changed the requested value; inspect final schema and retry",13);}break;}}phase=4;stage("invoke_plugin_accept");if(!QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection))finish("NOT_INVOKABLE","accept() is not Qt callable",10);return;
 }
 if(phase==4){
  if(dialog&&dialog->isVisible())return;
  if(protocolParent&&!parentFinalizing){parentFinalizing=true;dialog=protocolParent;stage("generate_serdes_parent_with_protocol");if(!QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection))finish("INTERFACE_MISSING","SerDes parent accept unavailable",10);return;}
  if(!outputSince)outputSince=QDateTime::currentMSecsSinceEpoch();
  if(QDateTime::currentMSecsSinceEpoch()-outputSince>700)finish("INVOCATION_COMPLETED","Plugin dialog completed; runner must validate output artifacts",0);
 }
}
static void boot(){const char*p=getenv("GOWIN_IP_BRIDGE_JOB");if(!p)return;QFile f(QString::fromUtf8(p));if(!f.open(QIODevice::ReadOnly))return;job=QJsonDocument::fromJson(f.readAll()).object();base=QFileInfo(f).absolutePath();started=QDateTime::currentMSecsSinceEpoch();result["route"]="qt_plugin_metaobject";result["qt_runtime"]=qVersion();result["job"]=job;QObject::connect(qApp,&QCoreApplication::aboutToQuit,[]{result["phase"]=phase;if(phase>=4){result["status"]="APPLICATION_EXITED_AFTER_ACCEPT";result["message"]="Runner must confirm IPC identity, applied parameters and generated HDL";}else result["status"]="APPLICATION_EXITED_BEFORE_ACCEPT";save();});QTimer::singleShot(1000,[]{timer=new QTimer(qApp);QObject::connect(timer,&QTimer::timeout,[]{tick();});timer->start(100);stage("waiting_for_ide");});}
Q_COREAPP_STARTUP_FUNCTION(boot)
