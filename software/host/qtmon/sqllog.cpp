
#include <tag.pb.h>
#include <tagdata.pb.h>
#include <tagclass.h>
#include <sqllog.h>
#include <chrono>
#include <QSqlQuery>
#include <QSqlError>
#include <google/protobuf/util/json_util.h>


int dumpTagSQL(QSqlDatabase *db, const Ack &log, const Config &config){
      return 0;
}

bool dumpTagSQLHeader(QSqlDatabase *db, Tag &tag){
  Config config;
  TagInfo info;
  CalibrationConstants constants;

  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
    // deprecated -- options.always_print_primitive_fields = true;
  options.preserve_proto_field_names = true;

  if (db) {

    QSqlQuery query(*db); // Can take the database object in its constructor: QSqlQuery query(db);

    // Define your SQL CREATE TABLE statement
    QString createTableSQL = "CREATE TABLE info ("
                             "id INTEGER PRIMARY KEY, "
                             "tagtype STRING, "
                             "info STRING, "
                             "config STRING, "
                             "calibration STRING"
                             ");";

    // Execute the query
    if (!query.exec(createTableSQL)) {
        qDebug() << "Failed to create table:" << query.lastError().text();
        return false;
    } else {
        qDebug() << "Table created";
    }

    tag.GetTagInfo(info);
    tag.GetConfig(config);
    std::string tagtype = TagType_Name(info.tag_type());
    std::string info_json;
    std::string config_json;
    std::string constants_json;

    

    if (!MessageToJsonString(info,&info_json,options).ok())
    {
        qDebug() << "Couldn't create json string for tag info";
        return false;
    }

    if (!MessageToJsonString(config,&config_json,options).ok())
    {
        qDebug() << "Couldn't create json string for tag config";
        return false;
    }

    if (tag.ReadCalibration(constants,-1))
    {
        if (!MessageToJsonString(constants,&constants_json,options).ok())
        {
            qDebug() << "Couldn't create json string for tag calibration constants";
            return false;
        }
    }
   
    query.prepare("INSERT INTO info (id, tagtype, info, config, calibration) "
                  "VALUES (:id, :type, :info, :config, :constants) ");
    query.bindValue(":id", 1);
    query.bindValue(":type", QString::fromStdString(tagtype));
    query.bindValue(":info", QString::fromStdString(info_json));
    query.bindValue(":config", QString::fromStdString(config_json));
    query.bindValue(":constants", QString::fromStdString(constants_json));
    if (!query.exec()) {
        qDebug() << "Insert failed: " << query.lastError().text();
    }


    return true;
  } else {
    qDebug() << "Null db pointer";
    return false;
  }
}