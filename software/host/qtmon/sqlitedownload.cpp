
#include "sqlitedownload.h"
#include <QtSql>
#include <QFile>
#include <google/protobuf/util/json_util.h>

SqliteDownload::SqliteDownload(Tag &t, QString &p, QObject *parent) : AbstractDownload(t, parent)
{
    qDebug() << "opening " << p;
    QFile::remove(p);
    QSqlDatabase db =  QSqlDatabase::addDatabase("QSQLITE","tagConnection");
    db.setDatabaseName(p);
    if (db.open()){
        qDebug() << "Database connected successfully";
    } else {
        qDebug() << "Couldn't open database file " << p <<  " error: " << db.lastError().text();
    }
    return;
}

SqliteDownload::~SqliteDownload(){
    //QSqlDatabase db = QSqlDatabase::database("tagConnection");
    //db.close();
    QSqlDatabase::removeDatabase("tagConnection");
}

bool SqliteDownload::dumpHeader(void) {
    QSqlDatabase db = QSqlDatabase::database("tagConnection");
    qDebug() << "writing header";
    TagInfo info;
    CalibrationConstants constants;

    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
        // deprecated -- options.always_print_primitive_fields = true;
    options.preserve_proto_field_names = true;

    if (db.isOpen()) {

        QSqlQuery query(db); // Can take the database object in its constructor: QSqlQuery query(db);

        // Define your SQL CREATE TABLE statement
        QString createTableSQL = "CREATE TABLE info ("
                                "fieldname STRING,"
                                "value STRING"
                                ");";

        // Execute the query
        if (!query.exec(createTableSQL)) {
            qDebug() << "Failed to create table:" << query.lastError().text();
            return false;
        } else {
            qDebug() << "Table created";
        }

        query.prepare("INSERT INTO info (fieldname, value) "
                    "VALUES (:fieldname, :value) ");


        tag.GetConfig(config);
        QString tagtype = QString::fromStdString(TagType_Name(config.tag_type()));
        qDebug() << "tag type = " << tagtype;

        query.bindValue(":fieldname","tagtype");
        query.bindValue(":value",tagtype);
        if (!query.exec()){
            qDebug() << "insert failed";
            return false;
        }

        tag.GetTagInfo(info);

        query.bindValue(":fieldname","uuid");
        query.bindValue(":value",QString::fromStdString(info.uuid()));
        if (!query.exec()){
            qDebug() << "insert failed";
            return false;
        }
        std::string info_json;
        std::string config_json;
        std::string constants_json;

        if (!MessageToJsonString(info,&info_json,options).ok())
        {
            qDebug() << "Couldn't create json string for tag info";
            return false;
        }
        query.bindValue(":fieldname","info");
        query.bindValue(":value",QString::fromStdString(info_json));
        if (!query.exec()){
            qDebug() << "insert failed";
            return false;
        }
        

        if (!MessageToJsonString(config,&config_json,options).ok())
        {
            qDebug() << "Couldn't create json string for tag config";
            return false;
        }
        query.bindValue(":fieldname", "config");
        query.bindValue(":value",QString::fromStdString(config_json));
        if (!query.exec()){
            qDebug() << "insert failed";
            return false;
        }

        // Build calibration table

        createTableSQL = "CREATE TABLE Calibration ("
                                "Epoch INTEGER,"
                                "Constants STRING);";
                                
         if (!query.exec(createTableSQL)) {
            qDebug() << "Failed to create calibration table:" << query.lastError().text();
            return false;
        }

        if (tag.ReadCalibration(constants,-1))
        {
            
        }

        query.prepare("INSERT INTO Calibration (Epoch, Constants) "
                      "VALUES (:epoch, :value) ");


        for (int i = 0; tag.ReadCalibration(constants,i); i++){
            constants_json = "";
            if (!MessageToJsonString(constants,&constants_json,options).ok())
            {
                qDebug() << "Couldn't create json string for tag calibration constants";
                return false;
            } 
            query.bindValue(":epoch",static_cast<qlonglong>(constants.timestamp()));
            query.bindValue(":value", QString::fromStdString(constants_json));
            if (!query.exec()){
                qDebug() << "Calibration constants insert failed";
                return false;
            }
        }

        // create states table

        
        createTableSQL = "CREATE TABLE states ("
                                "Epoch INTEGER,"
                                "Calibration STRING,"
                                "EntryCode STRING,"
                                "Temperature FLOAT,"
                                "Voltage FLOAT,"
                                "InternalLogSize INTEGER,"
                                "ExternalLogSize INTEGER"
                                ");";

        if (!query.exec(createTableSQL)) {
            qDebug() << "Failed to create state table:" << query.lastError().text();
            return false;
        }

        StateLog state_log;
        query.prepare("INSERT INTO states (Epoch, State, EntryCode, Temperature,"
                      " Voltage, InternalLogSize, ExternalLogSize) "
                      "VALUES (:epoch, :state, :entry,  :t, :v, :is, :es) ");
        
        int next = 0;
        while (tag.GetStateLog(state_log, next))
        {
            next += state_log.states().size();
            for (auto const &state : state_log.states())
            {
                query.bindValue(":epoch", static_cast<qlonglong>(state.status().millis())/1000);
                query.bindValue(":state", QString::fromStdString(TagState_Name(state.status().state())));
                query.bindValue(":entry",QString::fromStdString(State_Event_Name(state.transition_reason())));
                query.bindValue(":t", state.status().temperature());
                query.bindValue(":v", state.status().voltage());
                query.bindValue(":is",state.status().internal_data_count());
                query.bindValue(":es",state.status().external_data_count());
                if (!query.exec()) {
                    qDebug() << "State insertion failed";
                    return false;
                }
            }
        }
        return true;
    } else {
        qDebug() << "Null db pointer";
        return false;
    }
}

int SqliteDownload::dumpLog(Ack &ack) {
    qDebug() << "writing log file";
    if (ack.has_compasstag_data_log()){
        return dumpTagLog(ack.compasstag_data_log());
    }

    return 0;
}

int SqliteDownload::dumpTagLog(const CompassTagLog &log){
    QSqlDatabase db = QSqlDatabase::database("tagConnection");
    QSqlQuery query(db);
    if (!logTableCreated){
        // Create tables
        // core temperature
        QString createTableSQL = "CREATE TABLE CoreTemperature ("
                                "Epoch INTEGER,"
                                "Temperature FLOAT"
                                ");";
        query.exec(createTableSQL);
        // voltage
        createTableSQL = "CREATE TABLE Voltage ("
                        "Epoch INTEGER,"
                        "Voltage FLOAT"
                        ");";
        query.exec(createTableSQL);
        // activity
        createTableSQL = "CREATE TABLE Activity ("
                        "Epoch INTEGER,"
                        "Activity FLOAT"
                        ");";
        query.exec(createTableSQL);
        // compass
        createTableSQL = "CREATE TABLE Compass ("
                        "Epoch INTEGER,"
                        "ax FLOAT,"
                        "ay FLOAT,"
                        "az FLOAT,"
                        "mx FLOAT,"
                        "my FLOAT,"
                        "mz FLOAT"
                        ");";
        query.exec(createTableSQL);
        // Execute the query
        logTableCreated = true;
 
    }
    qlonglong timestamp = log.epoch();

    // voltage

    query.prepare("INSERT INTO Voltage (Epoch, Voltage) VALUES(:epoch, :v)");
    query.bindValue(":epoch", timestamp);
    query.bindValue(":v", log.voltage());
    query.exec();

    // temperature

    query.prepare("INSERT INTO CoreTemperature (Epoch, Temperature) VALUES(:epoch, :t)");
    query.bindValue(":epoch", timestamp);
    query.bindValue(":t", log.temperature());
    query.exec();

    for (auto const &entry : log.data()) {
        timestamp += 15;

        // Activity

        query.prepare("INSERT INTO Activity (Epoch, Activity) VALUES(:epoch, :a)");
        query.bindValue(":epoch", timestamp);
        query.bindValue(":a", entry.activity());
        query.exec();

        // Compass

        query.prepare("INSERT INTO Compass (Epoch, ax, ay, az, mx, my, mz)"
                      "VALUES(:epoch, :ax, :ay, :az, :mx, :my, :mz)");
        query.bindValue(":epoch", timestamp);
        query.bindValue(":ax", entry.ax());
        query.bindValue(":ay", entry.ay());
        query.bindValue(":az", entry.az());
        query.bindValue(":mx", entry.mx());
        query.bindValue(":my", entry.my());
        query.bindValue(":mz", entry.mz());
        query.exec();
    }
     
    return 1;
}