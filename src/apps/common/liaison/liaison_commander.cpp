// copyright 2011 t. schneider tes@mit.edu
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software.  If not, see <http://www.gnu.org/licenses/>.

#include <cfloat>
#include <cmath>

#include <Wt/Dbo/Exception>
#include <Wt/WIntValidator>
#include <Wt/WDoubleValidator>
#include <Wt/WRegExpValidator>
#include <Wt/WPanel>
#include <Wt/WLengthValidator>

#include "goby/util/dynamic_protobuf_manager.h"
#include "goby/util/sci.h"
#include "goby/moos/moos_protobuf_helpers.h"

#include "liaison_commander.h"

using namespace Wt;
using namespace goby::util::logger;
using goby::util::logger_lock::lock;
using goby::glog;
using goby::moos::operator<<;

boost::mutex goby::common::LiaisonCommander::dbo_mutex_;
boost::shared_ptr<Dbo::backend::Sqlite3> goby::common::LiaisonCommander::sqlite3_;
boost::shared_ptr<Dbo::FixedSqlConnectionPool> goby::common::LiaisonCommander::connection_pool_;
boost::posix_time::ptime goby::common::LiaisonCommander::last_db_update_time_(goby::util::goby_time());


const std::string MESSAGE_INCLUDE_TEXT = "include";
const std::string MESSAGE_REMOVE_TEXT = "remove";

const std::string STRIPE_ODD_CLASS = "odd";
const std::string STRIPE_EVEN_CLASS = "even";


goby::common::LiaisonCommander::LiaisonCommander(ZeroMQService* zeromq_service,
                                                 WContainerWidget* parent)
    : LiaisonContainer(parent),
      MOOSNode(zeromq_service),
      zeromq_service_(zeromq_service),
      pb_commander_config_(Liaison::cfg_.GetExtension(protobuf::pb_commander_config)),
      main_layout_(new WVBoxLayout(this)),
      commands_div_(new WStackedWidget(this)),
      controls_div_(new ControlsContainer(this, pb_commander_config_, commands_div_, this))
{
    protobuf::ZeroMQServiceConfig ipc_sockets;
    protobuf::ZeroMQServiceConfig::Socket* internal_subscribe_socket = ipc_sockets.add_socket();
    internal_subscribe_socket->set_socket_type(protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
    internal_subscribe_socket->set_socket_id(Liaison::LIAISON_INTERNAL_COMMANDER_SUBSCRIBE_SOCKET);
    internal_subscribe_socket->set_transport(protobuf::ZeroMQServiceConfig::Socket::INPROC);
    internal_subscribe_socket->set_connect_or_bind(protobuf::ZeroMQServiceConfig::Socket::CONNECT);
    internal_subscribe_socket->set_socket_name(Liaison::LIAISON_INTERNAL_PUBLISH_SOCKET_NAME);

    protobuf::ZeroMQServiceConfig::Socket* internal_publish_socket = ipc_sockets.add_socket();
    internal_publish_socket->set_socket_type(protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
    internal_publish_socket->set_socket_id(Liaison::LIAISON_INTERNAL_COMMANDER_PUBLISH_SOCKET);
    internal_publish_socket->set_transport(protobuf::ZeroMQServiceConfig::Socket::INPROC);
    internal_publish_socket->set_connect_or_bind(protobuf::ZeroMQServiceConfig::Socket::CONNECT);
    internal_publish_socket->set_socket_name(Liaison::LIAISON_INTERNAL_SUBSCRIBE_SOCKET_NAME);

    zeromq_service_->merge_cfg(ipc_sockets);    

    main_layout_->addWidget(controls_div_);
    main_layout_->addWidget(commands_div_);
    main_layout_->addStretch(1);

    for(int i = 0, n = pb_commander_config_.subscription_size(); i < n; ++i)
        subscribe(pb_commander_config_.subscription(i), Liaison::LIAISON_INTERNAL_COMMANDER_SUBSCRIBE_SOCKET);
    
}

void goby::common::LiaisonCommander::moos_inbox(CMOOSMsg& msg)
{
    glog.is(DEBUG1, lock) && glog << "LiaisonCommander: Got message: " << msg <<  std::endl << unlock;

    WContainerWidget* new_div = new WContainerWidget(controls_div_->incoming_message_stack_);
    
    WPushButton* minus = new WPushButton("-", new_div);
    WPushButton* plus = new WPushButton("+", new_div);

    new WText("Message: " + goby::util::as<std::string>(controls_div_->incoming_message_stack_->children().size()-1), new_div);

    plus->clicked().connect(controls_div_, &ControlsContainer::increment_incoming_messages);
    minus->clicked().connect(controls_div_, &ControlsContainer::decrement_incoming_messages);    
    
    WGroupBox* box = new WGroupBox(msg.GetKey() + " @ " +
                                   boost::posix_time::to_simple_string(
                                       goby::util::as<boost::posix_time::ptime>(
                                           msg.GetTime())),
                                   new_div);


    std::string value = msg.GetAsString();
    
    boost::shared_ptr<google::protobuf::Message> pb_msg =
        dynamic_parse_for_moos(value);

    if(pb_msg)
        new WText("<pre>" + pb_msg->DebugString() + "</pre>", box);
    else
        new WText("<pre>" + value + "</pre>", box);
        
    controls_div_->incoming_message_stack_->setCurrentIndex(controls_div_->incoming_message_stack_->children().size()-1);
}

void goby::common::LiaisonCommander::ControlsContainer::increment_incoming_messages(const WMouseEvent& event)
{
    int new_index = incoming_message_stack_->currentIndex()+1;
    if(new_index == static_cast<int>(incoming_message_stack_->children().size()))
        new_index = 0;
    
    incoming_message_stack_->setCurrentIndex(new_index);
}


void goby::common::LiaisonCommander::ControlsContainer::decrement_incoming_messages(const WMouseEvent& event)
{
    int new_index = static_cast<int>(incoming_message_stack_->currentIndex())-1;
    if(new_index < 0 )
        new_index = incoming_message_stack_->children().size()-1;
    
    incoming_message_stack_->setCurrentIndex(new_index);
}




void goby::common::LiaisonCommander::loop()
{
    ControlsContainer::CommandContainer* current_command =
        dynamic_cast<ControlsContainer::CommandContainer*>(
            controls_div_->commands_div_->currentWidget());

    while(zeromq_service_->poll(0))
    { }
    
    
    
    if(current_command && (last_db_update_time_ > current_command->last_reload_time_))
    {
        glog.is(DEBUG1, lock) && glog << "Reloading command!" <<  std::endl << unlock;
        glog.is(DEBUG1, lock) && glog << last_db_update_time_ << "/" <<  current_command->last_reload_time_ << unlock;
    
        boost::mutex::scoped_lock slock(dbo_mutex_);
        Dbo::Transaction transaction(controls_div_->session_);
        current_command->query_model_->reload();
        current_command->last_reload_time_ = goby::util::goby_time();
    }    
}

goby::common::LiaisonCommander::ControlsContainer::ControlsContainer(
    MOOSNode* moos_node,
    const protobuf::ProtobufCommanderConfig& pb_commander_config,
    WStackedWidget* commands_div,
    WContainerWidget* parent /*=0*/)
    : WGroupBox("Controls"),
      moos_node_(moos_node),
      pb_commander_config_(pb_commander_config),
      command_label_(new WLabel("Message: ", this)),
      command_selection_(new WComboBox(this)),
      buttons_div_(new WContainerWidget(this)),
      comment_label_(new WLabel("Log comment: ", buttons_div_)),
      comment_line_(new WLineEdit(buttons_div_)),
      send_button_(new WPushButton("Send", buttons_div_)),
      clear_button_(new WPushButton("Clear", buttons_div_)),
      commands_div_(commands_div),
      incoming_message_panel_(new Wt::WPanel(this)),
      incoming_message_stack_(new Wt::WStackedWidget),
      master_field_info_panel_(new Wt::WPanel(this)),
      master_field_info_stack_(new Wt::WStackedWidget)
{
    // if we're the first thread, make the database connection
    if(!sqlite3_)
    {
        boost::mutex::scoped_lock slock(dbo_mutex_);
        sqlite3_.reset(new Dbo::backend::Sqlite3(pb_commander_config_.sqlite3_database()));
        connection_pool_.reset(new Dbo::FixedSqlConnectionPool(sqlite3_.get(), pb_commander_config_.database_pool_size()));
    }        

    session_.setConnectionPool(*connection_pool_);
    session_.mapClass<CommandEntry>("_liaison_commands");
    
    try
    {    
        session_.createTables();
    }
    catch(Dbo::Exception& e)
    {
        glog.is(VERBOSE, lock) && glog << "Could not create tables: " << e.what() << std::endl << unlock;   
    }

    
//    incoming_message_panel_->setPositionScheme(Wt::Fixed);
//    incoming_message_panel_->setOffsets(20, Wt::Left | Wt::Bottom);

    incoming_message_panel_->setTitle("Incoming messages");
    incoming_message_panel_->setCollapsible(true);
    incoming_message_panel_->setCentralWidget(incoming_message_stack_);
    incoming_message_panel_->addStyleClass("fixed-left");

//    Wt::WCssDecorationStyle field_info_style;    
//    field_info_style.setBackgroundColor(WColor(white));

    master_field_info_panel_->setTitle("Field Information");
    master_field_info_panel_->setCollapsible(true);
    master_field_info_panel_->setCentralWidget(master_field_info_stack_);
    
//    master_field_info_panel_->setDecorationStyle(field_info_style);
    
//    master_field_info_panel_->setPositionScheme(Wt::Fixed);
//    master_field_info_panel_->setOffsets(20, Wt::Right | Wt::Bottom);
    master_field_info_panel_->addStyleClass("fixed-right");


    
    send_button_->setDisabled(true);
    clear_button_->setDisabled(true);
    comment_line_->setDisabled(true);
    
    comment_label_->setBuddy(comment_line_);
    
    command_selection_->addItem("(Select a command message)");
    send_button_->clicked().connect(this, &ControlsContainer::send_message);
    clear_button_->clicked().connect(this, &ControlsContainer::clear_message);

    Dbo::ptr<CommandEntry> last_command(static_cast<goby::common::CommandEntry*>(0));
    {    
        boost::mutex::scoped_lock slock(dbo_mutex_);
        Dbo::Transaction transaction(session_);            
        last_command = session_.find<CommandEntry>("ORDER BY time DESC LIMIT 1");
        if(last_command)
            glog.is(DEBUG1, lock) && glog << "Last command was of type: " << last_command->protobuf_name << std::endl << unlock;
            
    }
    
    command_selection_->activated().connect(this, &ControlsContainer::switch_command);

    for(int i = 0, n = pb_commander_config.load_protobuf_name_size(); i < n; ++i)
    {
        const google::protobuf::Descriptor* desc =
            goby::util::DynamicProtobufManager::descriptor_pool().FindMessageTypeByName(
                pb_commander_config.load_protobuf_name(i));

        
        if(!desc)
        {
            glog.is(WARN, lock) && glog << "Could not find protobuf name " << pb_commander_config.load_protobuf_name(i) << " to load for Protobuf Commander (configuration line `load_protobuf_name`)" << std::endl << unlock;
        }
        else
        {
            command_selection_->addItem(pb_commander_config.load_protobuf_name(i));
        }
    }    

    command_selection_->model()->sort(0);
    
    if(last_command)
    {                
        command_selection_->setCurrentIndex(
            command_selection_->findText(last_command->protobuf_name));
        switch_command(command_selection_->currentIndex());
    }
}

void goby::common::LiaisonCommander::ControlsContainer::switch_command(int selection_index)
{
    if(selection_index == 0)
    {
        send_button_->setDisabled(true);
        clear_button_->setDisabled(true);
        comment_line_->setDisabled(true);
        return;
    }
    
    send_button_->setDisabled(false);
    clear_button_->setDisabled(false);
    comment_line_->setDisabled(false);
    
    std::string protobuf_name = command_selection_->itemText(selection_index).narrow();

    if(!commands_.count(protobuf_name))
    {
        CommandContainer* new_command = new CommandContainer(pb_commander_config_,
                                                             protobuf_name,
                                                             &session_,
                                                             master_field_info_stack_);
        commands_div_->addWidget(new_command);
        // index of the newly added widget
        commands_[protobuf_name] = commands_div_->count() - 1;
    }
    commands_div_->setCurrentIndex(commands_[protobuf_name]);
    master_field_info_stack_->setCurrentIndex(commands_[protobuf_name]);
}

void goby::common::LiaisonCommander::ControlsContainer::clear_message()
{
    WDialog dialog("Confirm clearing of message: " + command_selection_->currentText());
    WPushButton ok("Clear", dialog.contents());
    WPushButton cancel("Cancel", dialog.contents());

    dialog.rejectWhenEscapePressed();
    ok.clicked().connect(&dialog, &WDialog::accept);
    cancel.clicked().connect(&dialog, &WDialog::reject);
    
    if (dialog.exec() == WDialog::Accepted)
    {
        CommandContainer* current_command = dynamic_cast<CommandContainer*>(commands_div_->currentWidget());
        current_command->message_->Clear();
        current_command->generate_root();
        
    }
 }

void goby::common::LiaisonCommander::ControlsContainer::send_message()
{
    glog.is(VERBOSE, lock) && glog << "Message to be sent!" << std::endl << unlock;

    WDialog dialog("Confirm sending of message: " + command_selection_->currentText());

    CommandContainer* current_command = dynamic_cast<CommandContainer*>(commands_div_->currentWidget());

    WGroupBox* comment_box = new WGroupBox("Log comment", dialog.contents());
    WLineEdit* comment_line = new WLineEdit(comment_box);
    comment_line->setText(comment_line_->text());
    
    WGroupBox* message_box = new WGroupBox("Message to send", dialog.contents());
    new WText("<pre>" + current_command->message_->DebugString() + "</pre>", message_box);

    message_box->setMaximumSize(800, 600);
    message_box->setOverflow(WContainerWidget::OverflowAuto);

//    dialog.setResizable(true);
    
    WPushButton ok("Send", dialog.contents());
    WPushButton cancel("Cancel", dialog.contents());

    dialog.rejectWhenEscapePressed();

    ok.clicked().connect(&dialog, &WDialog::accept);
    cancel.clicked().connect(&dialog, &WDialog::reject);

    if (dialog.exec() == WDialog::Accepted)
    {
        std::string serialized; 
        serialize_for_moos(&serialized, *current_command->message_);
        moos_node_->send(CMOOSMsg(MOOS_NOTIFY, "LIAISON_COMMANDER_OUT", serialized),
             Liaison::LIAISON_INTERNAL_COMMANDER_PUBLISH_SOCKET);
        
            
        CommandEntry* command_entry = new CommandEntry;
        command_entry->protobuf_name = current_command->message_->GetDescriptor()->full_name();
        command_entry->bytes.resize(current_command->message_->ByteSize());
        current_command->message_->SerializeToArray(
            &command_entry->bytes[0], command_entry->bytes.size());
        command_entry->address = wApp->environment().clientAddress();
        
        boost::posix_time::ptime now = goby::util::goby_time();
        command_entry->time.setPosixTime(now);
        command_entry->comment = comment_line->text().narrow();
        session_.add(command_entry);

        {
            boost::mutex::scoped_lock slock(dbo_mutex_);
            Dbo::Transaction transaction(*current_command->session_);            
            transaction.commit();
            last_db_update_time_ = now;
        }
        
        comment_line_->setText("");
        current_command->query_model_->reload();
    }

}


goby::common::LiaisonCommander::ControlsContainer::CommandContainer::CommandContainer(
    const protobuf::ProtobufCommanderConfig& pb_commander_config,
    const std::string& protobuf_name,
    Dbo::Session* session,
    WStackedWidget* master_field_info_stack)
    : WGroupBox(protobuf_name),
      message_(goby::util::DynamicProtobufManager::new_protobuf_message(protobuf_name)),
      tree_box_(new WGroupBox("Contents", this)),
      tree_table_(new WTreeTable(tree_box_)),
      field_info_stack_(new WStackedWidget(master_field_info_stack)),
      session_(session),
      query_model_(new Dbo::QueryModel<Dbo::ptr<CommandEntry> >(this)),
      query_box_(new WGroupBox("Message log (click for details)", this)),
      query_table_(new WTreeView(query_box_)),
      last_reload_time_(boost::posix_time::neg_infin)
{


    
    new WText("", field_info_stack_);
    field_info_map_[0] = 0;
    
    tree_table_->addColumn("Value", pb_commander_config.value_width_pixels());
    tree_table_->addColumn("Modify", pb_commander_config.modify_width_pixels());

    {
        boost::mutex::scoped_lock slock(dbo_mutex_);
        Dbo::Transaction transaction(*session_);
        query_model_->setQuery(session_->find<CommandEntry>("where protobuf_name='" + protobuf_name  + "'"));
    }

    
    
    query_model_->addColumn("comment", "Comment");
    query_model_->addColumn("protobuf_name", "Name");
    query_model_->addColumn("address", "Network Address");
    query_model_->addColumn("time", "Time");
    
    query_table_->setModel(query_model_);
    query_table_->resize(WLength::Auto,
                         pb_commander_config.database_view_height());
    query_table_->sortByColumn(protobuf::ProtobufCommanderConfig::COLUMN_TIME,
                               DescendingOrder);
    query_table_->setMinimumSize(pb_commander_config.database_width().comment_width()+
                                 pb_commander_config.database_width().name_width()+
                                 pb_commander_config.database_width().ip_width()+
                                 pb_commander_config.database_width().time_width()+
                                 7*(protobuf::MOOSScopeConfig::COLUMN_MAX+1),
                                 WLength::Auto);    

    
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_COMMENT,
                                 pb_commander_config.database_width().comment_width());
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_NAME,
                                 pb_commander_config.database_width().name_width());
    
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_IP,
                                 pb_commander_config.database_width().ip_width());
    
    query_table_->setColumnWidth(protobuf::ProtobufCommanderConfig::COLUMN_TIME,
                                 pb_commander_config.database_width().time_width());
    
    query_table_->clicked().connect(this, &CommandContainer::handle_database_double_click);

    
    
    if(query_model_->rowCount() > 0)
    {
        const Dbo::ptr<CommandEntry>& entry = query_model_->resultRow(0);
        message_->ParseFromArray(&entry->bytes[0], entry->bytes.size());
    }
    
    glog.is(DEBUG1, lock) && glog << "Model has "<< query_model_->rowCount() << " rows" <<  std::endl << unlock;

    
    generate_root();
}

void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_database_double_click(const WModelIndex& index, const WMouseEvent& event)
 {    
     glog.is(DEBUG1, lock) && glog << "clicked: " << index.row() << "," << index.column() << std::endl << unlock;    

     const Dbo::ptr<CommandEntry>& entry = query_model_->resultRow(index.row());
     
     boost::shared_ptr<google::protobuf::Message> message(message_->New());
     message->ParseFromArray(&entry->bytes[0], entry->bytes.size());

     if(!message)
     {
         glog.is(WARN, lock) && glog << "Invalid message!" <<  std::endl << unlock;
         return;
     }
     
     database_dialog_.reset(new WDialog("Viewing log entry: " + entry->protobuf_name + " posted at " + entry->time.toString()));     
     
     WGroupBox* comment_box = new WGroupBox("Log comment", database_dialog_->contents());
     new WText(entry->comment, comment_box);
     
     WGroupBox* message_box = new WGroupBox("Message posted", database_dialog_->contents());
     new WText("<pre>" + message->DebugString() + "</pre>", message_box);
     
     message_box->setMaximumSize(800, 600);
     message_box->setOverflow(WContainerWidget::OverflowAuto);


     WPushButton* edit = new WPushButton("Edit (replace)", database_dialog_->contents());
     WPushButton* merge = new WPushButton("Edit (merge)", database_dialog_->contents());
     WPushButton* cancel = new WPushButton("Cancel", database_dialog_->contents());
     
     database_dialog_->rejectWhenEscapePressed();

     edit->clicked().connect(boost::bind(&CommandContainer::handle_database_dialog,
                                        this, RESPONSE_EDIT, message));
     merge->clicked().connect(boost::bind(&CommandContainer::handle_database_dialog,
                                         this, RESPONSE_MERGE, message));
     cancel->clicked().connect(boost::bind(&CommandContainer::handle_database_dialog,
                                          this, RESPONSE_CANCEL, message));
     
     
     database_dialog_->show();
//     merge.clicked().connect(&dialog, &WDialog::accept);
//     cancel.clicked().connect(&dialog, &WDialog::reject);

     
     
 }

void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_database_dialog(
    DatabaseDialogResponse response,
    boost::shared_ptr<google::protobuf::Message> message)
{
    switch(response)
    {
        case RESPONSE_EDIT:
            message_->CopyFrom(*message);
            generate_root();
            database_dialog_->accept();
            break;

        case RESPONSE_MERGE:
            message->MergeFrom(*message_);
            message_->CopyFrom(*message);
            generate_root();    
            database_dialog_->accept();
            break;

        case RESPONSE_CANCEL:
            database_dialog_->reject();
            break;        
    }
}




void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_root()
{
    const google::protobuf::Descriptor* desc = message_->GetDescriptor();
    
    // Create and set the root node
    WTreeTableNode *root = new WTreeTableNode(desc->name());
    root->setImagePack("resources/");
    root->expand();
    root->setStyleClass(STRIPE_EVEN_CLASS);    

    // deletes an existing root
    tree_table_->setTreeRoot(root, "Field");
    
    generate_tree(root, message_.get());
}


void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree(
    WTreeTableNode* parent,
    google::protobuf::Message* message)
{
    const google::protobuf::Descriptor* desc = message->GetDescriptor();
    
    for(int i = 0, n = desc->field_count(); i < n; ++i)
        generate_tree_row(parent, message, desc->field(i));
    
    std::vector<const google::protobuf::FieldDescriptor*> extensions;
    goby::util::DynamicProtobufManager::descriptor_pool().FindAllExtensions(desc, &extensions);
    for(int i = 0, n = extensions.size(); i < n; ++i)
        generate_tree_row(parent, message, extensions[i]);

}

void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree_row(
    WTreeTableNode* parent,
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    int index = parent->childNodes().size();
    WTreeTableNode* node = new WTreeTableNode(field_desc->name(), 0, parent);

    if((parent->styleClass() == STRIPE_ODD_CLASS && index % 2) || (parent->styleClass() == STRIPE_EVEN_CLASS && !(index % 2)))
        node->setStyleClass(STRIPE_ODD_CLASS);
    else
        node->setStyleClass(STRIPE_EVEN_CLASS);
    
    
    WFormWidget* value_field = 0;
    WFormWidget* modify_field = 0;
    if(field_desc->is_repeated())
    {
//        WContainerWidget* div = new WContainerWidget;
//        WLabel* label = new WLabel(": ", div); 
        WSpinBox* spin_box = new WSpinBox;
        spin_box->setTextSize(3);
        //       label->setBuddy(spin_box);
        spin_box->setRange(0, std::numeric_limits<int>::max());
        spin_box->setSingleStep(1);

        spin_box->valueChanged().connect(
            boost::bind(&CommandContainer::handle_repeated_size_change,
                        this, _1, message, field_desc, node));

        spin_box->setValue(refl->FieldSize(*message, field_desc));
        
        modify_field = spin_box; 
    }    
    else
    {
        if(field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            if(field_desc->is_required())
            {
                generate_tree(node,
                              message->GetReflection()->MutableMessage(message, field_desc));
                node->expand();
            }
            else
            {   
                WPushButton* button = new WPushButton(MESSAGE_INCLUDE_TEXT);
                    
                button->clicked().connect(
                    boost::bind(&CommandContainer::handle_toggle_single_message,
                                this, _1, message, field_desc, button, node));

                if(refl->HasField(*message, field_desc))
                {
                    parent->expand();
                    handle_toggle_single_message(WMouseEvent(),
                                                 message, field_desc, button, node);

                }
                
                    
                modify_field = button;
            }
        }
        else
        {
            generate_tree_field(value_field, message, field_desc);
        }
    }
    if(value_field)
        node->setColumnWidget(1, value_field);
    
    if(modify_field)
    {
        if(is_dccl(*message))
            dccl_default_modify_field(modify_field, field_desc);        

        generate_field_info_box(modify_field, field_desc);

        node->setColumnWidget(2, modify_field);
    }
    
}


void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_tree_field(
    WFormWidget*& value_field,
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc,
    int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    switch(field_desc->cpp_type())
    {
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            break;

        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        {
            WIntValidator* validator = new WIntValidator;

            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddInt32(message, field_desc, field_desc->default_value_int32());
            
            int32 value = field_desc->is_repeated() ?
                refl->GetRepeatedInt32(*message, field_desc, index) :
                refl->GetInt32(*message, field_desc);
            
            value_field = generate_single_line_edit_field(
                message,
                field_desc,
                goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_int32()),
                validator,
                index);
        }
        break;
        
        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
        {
            WIntValidator* validator = new WIntValidator;

            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddInt64(message, field_desc, field_desc->default_value_int64());
            
            int64 value = field_desc->is_repeated() ?
                refl->GetRepeatedInt64(*message, field_desc, index) :
                refl->GetInt64(*message, field_desc);
            
            value_field = generate_single_line_edit_field(
                message,
                field_desc,
                goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_int64()),
                validator,
                index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
        {
            WIntValidator* validator = new WIntValidator;
            validator->setBottom(0);

            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddUInt32(message, field_desc, field_desc->default_value_uint32());
            
            uint32 value = field_desc->is_repeated() ?
                refl->GetRepeatedUInt32(*message, field_desc, index) :
                refl->GetUInt32(*message, field_desc);
            
            value_field = generate_single_line_edit_field(
                message,
                field_desc,
                goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_uint32()),
                validator,
                index);
        }
        break;

        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
        {
            WIntValidator* validator = new WIntValidator;
            validator->setBottom(0);

            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddUInt64(message, field_desc, field_desc->default_value_uint64());
            
            uint64 value = field_desc->is_repeated() ?
                refl->GetRepeatedUInt64(*message, field_desc, index) :
                refl->GetUInt64(*message, field_desc);
            
            value_field = generate_single_line_edit_field(
                message,
                field_desc,
                goby::util::as<std::string>(value),
                goby::util::as<std::string>(field_desc->default_value_uint64()),
                validator,
                index);
        }
        break;
            
        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        {
            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddString(message, field_desc, field_desc->default_value_string());

            
            WValidator* validator = 0;
            
            std::string current_str = field_desc->is_repeated() ?
                refl->GetRepeatedString(*message, field_desc, index) :
                refl->GetString(*message, field_desc);
            std::string default_str = field_desc->default_value_string();
                
            if(field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
            {
                validator = new WRegExpValidator("([0-9,a-f,A-F][0-9,a-f,A-F])*");
                 
                current_str = goby::util::hex_encode(current_str);
                default_str = goby::util::hex_encode(default_str);
            }
            else
            {
                validator = new WValidator;
            }
            
                

            value_field = generate_single_line_edit_field(message,
                                                          field_desc,
                                                          current_str,
                                                          default_str,
                                                          validator,
                                                          index);
        }
        break;
                
        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
        {
            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddFloat(message, field_desc, field_desc->default_value_float());

            float value = field_desc->is_repeated() ?
                refl->GetRepeatedFloat(*message, field_desc, index) :
                refl->GetFloat(*message, field_desc);
            
            WDoubleValidator* validator = new WDoubleValidator;
            validator->setRange(std::numeric_limits<float>::min(),
                                std::numeric_limits<float>::max());                
                
            value_field = generate_single_line_edit_field(
                message,
                field_desc,
                goby::util::as<std::string>(value, std::numeric_limits<float>::digits10),
                goby::util::as<std::string>(field_desc->default_value_float(),
                                            std::numeric_limits<float>::digits10),
                validator,
                index);
        }
        break;
                
        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
        {
            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddDouble(message, field_desc, field_desc->default_value_double());

            double value = field_desc->is_repeated() ?
                refl->GetRepeatedDouble(*message, field_desc, index) :
                refl->GetDouble(*message, field_desc);
            
            WDoubleValidator* validator = new WDoubleValidator;
                
            value_field = generate_single_line_edit_field(
                message,
                field_desc,
                goby::util::as<std::string>(value, std::numeric_limits<double>::digits10),
                goby::util::as<std::string>(field_desc->default_value_double(),
                                            std::numeric_limits<double>::digits10),
                validator,
                index);
        }
        break;
            
                        
        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
        {
            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddBool(message, field_desc, field_desc->default_value_bool());
             
            bool value = field_desc->is_repeated() ?
                refl->GetRepeatedBool(*message, field_desc, index) :
                refl->GetBool(*message, field_desc);
            
            std::vector<WString> strings;
            strings.push_back("true");
            strings.push_back("false");
                
            value_field = generate_combo_box_field(
                message,
                field_desc,
                strings,
                value ? 0 : 1,
                goby::util::as<std::string>(field_desc->default_value_bool()),
                index); 
        }
        break;

                
        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
        {
            if(field_desc->is_repeated() && refl->FieldSize(*message, field_desc) <= index)
                refl->AddEnum(message, field_desc, field_desc->default_value_enum());

            const google::protobuf::EnumValueDescriptor* value = field_desc->is_repeated() ?
                refl->GetRepeatedEnum(*message, field_desc, index) :
                refl->GetEnum(*message, field_desc);
            
            std::vector<WString> strings;

            const google::protobuf::EnumDescriptor* enum_desc = field_desc->enum_type();
                
            for(int i = 0, n = enum_desc->value_count(); i < n; ++i)
                strings.push_back(enum_desc->value(i)->name());                
                
            value_field = generate_combo_box_field(
                message,
                field_desc,
                strings,
                value->index(),
                goby::util::as<std::string>(field_desc->default_value_enum()->name()),
                index);
        } 
        break;
            
    }
    
    if(is_dccl(*message))
        dccl_default_value_field(value_field, field_desc);

    generate_field_info_box(value_field, field_desc);
    
}
void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_field_info_box(Wt::WFormWidget*& value_field, const google::protobuf::FieldDescriptor* field_desc)
{   
    if(!field_info_map_.count(field_desc))
    {
        WGroupBox* box = new WGroupBox(field_desc->full_name(), field_info_stack_);
        new WText("[Field] " + field_desc->DebugString(), box);

        std::vector<const google::protobuf::FieldDescriptor*> extensions;
        google::protobuf::DescriptorPool::generated_pool()->FindAllExtensions(field_desc->options().GetDescriptor(), &extensions);
        for(int i = 0, n = extensions.size(); i < n; ++i)
        {
            const google::protobuf::FieldDescriptor* ext_field_desc = extensions[i];
            if(!ext_field_desc->is_repeated() &&
               field_desc->options().GetReflection()->HasField(field_desc->options(), ext_field_desc))
            {
                std::string ext_str;
                google::protobuf::TextFormat::PrintFieldValueToString(field_desc->options(),
                                                                      ext_field_desc,
                                                                      -1,
                                                                      &ext_str);
                new WText("<br/> [Options] " + ext_field_desc->full_name() + ": " + ext_str, box);
            }
        }
        
        
        field_info_map_.insert(std::make_pair(field_desc, field_info_map_.size()));
    }

    value_field->focussed().connect(boost::bind(&CommandContainer::handle_field_focus, this,
                                                field_info_map_[field_desc]));
//  value_field->blurred().connect(boost::bind(&CommandContainer::handle_field_focus, this, 0));
    
}



void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_line_field_changed(google::protobuf::Message* message, const google::protobuf::FieldDescriptor* field_desc, WLineEdit* field, int index)
{
    std::string value = field->text().narrow();

    const google::protobuf::Reflection* refl = message->GetReflection();
    
    if(value.empty() && field_desc->is_repeated())
        value = field->emptyText().narrow();

    
    if(value.empty() && !field_desc->is_repeated())
    {
        refl->ClearField(message, field_desc);
    }
    else
    {
        switch(field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                field_desc->is_repeated() ?
                    refl->SetRepeatedInt32(message, field_desc, index, goby::util::as<int32>(value)) :
                    refl->SetInt32(message, field_desc, goby::util::as<int32>(value));
                break;
                    
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                field_desc->is_repeated() ?
                    refl->SetRepeatedInt64(message, field_desc, index, goby::util::as<int64>(value)) :
                    refl->SetInt64(message, field_desc, goby::util::as<int64>(value));
                break;

            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                field_desc->is_repeated() ?
                    refl->SetRepeatedUInt32(message, field_desc, index, goby::util::as<uint32>(value)) :
                    refl->SetUInt32(message, field_desc, goby::util::as<uint32>(value));
                break;
                    
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                field_desc->is_repeated() ?
                    refl->SetRepeatedUInt64(message, field_desc, index, goby::util::as<uint64>(value)) :
                    refl->SetUInt64(message, field_desc, goby::util::as<uint64>(value));
                break;
                    
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                
                if(field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
                {
                    value = goby::util::hex_decode(value);
                }

                field_desc->is_repeated() ?
                    refl->SetRepeatedString(message, field_desc, index, value) :
                    refl->SetString(message, field_desc, value);
                break;                    
                
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            {
                double fvalue = goby::util::as<float>(value);

                if(is_dccl(*message))
                    field->setText(string_from_dccl_double(&fvalue, field_desc));     

                field_desc->is_repeated() ?
                    refl->SetRepeatedFloat(message, field_desc, index, fvalue) :
                    refl->SetFloat(message, field_desc, fvalue);
            }
            break;
                
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
            {
                double dvalue = goby::util::as<double>(value);
                
                if(is_dccl(*message))
                    field->setText(string_from_dccl_double(&dvalue, field_desc));
                
                field_desc->is_repeated() ?
                    refl->SetRepeatedDouble(message, field_desc, index, dvalue) :
                    refl->SetDouble(message, field_desc, dvalue);
            }
            break;

            default:
                break;
        }
    }
    
}


void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_combo_field_changed(google::protobuf::Message* message, const google::protobuf::FieldDescriptor* field_desc, WComboBox* field, int index)
{
    const google::protobuf::Reflection* refl = message->GetReflection();
    
    
    if(field->currentIndex() == 0)
        refl->ClearField(message, field_desc);
    else
    {
        std::string value = field->currentText().narrow();

        switch(field_desc->cpp_type())
        {
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                field_desc->is_repeated() ?
                    refl->SetRepeatedBool(message, field_desc, index, goby::util::as<bool>(value)) :
                    refl->SetBool(message, field_desc, goby::util::as<bool>(value));
                break;
                    
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                field_desc->is_repeated() ?
                    refl->SetRepeatedEnum(message, field_desc, index, field_desc->enum_type()->FindValueByName(value)) :
                    refl->SetEnum(message, field_desc, field_desc->enum_type()->FindValueByName(value));
                break;

            default:
                break;    
        }
    }
    glog.is(DEBUG1, lock) && glog << "The message is: " << message_->DebugString() <<  std::endl << unlock;
}

                    

WLineEdit* goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_single_line_edit_field(
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc,
    const std::string& current_value,
    const std::string& default_value,
    WValidator* validator /* = 0*/,
    int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    WLineEdit* line_edit = new WLineEdit();
    
    if(field_desc->has_default_value() || field_desc->is_repeated())
        line_edit->setEmptyText(default_value);

    if((!field_desc->is_repeated() && refl->HasField(*message, field_desc)) ||
       (field_desc->is_repeated() && index < refl->FieldSize(*message, field_desc)))
        line_edit->setText(current_value);    

    
    if(validator)
    {
        validator->setMandatory(field_desc->is_required());
        line_edit->setValidator(validator);
    }
    
    line_edit->changed().connect(boost::bind(&CommandContainer::handle_line_field_changed,
                                             this, message, field_desc, line_edit, index));


    return line_edit;
}

 WComboBox* goby::common::LiaisonCommander::ControlsContainer::CommandContainer::generate_combo_box_field(
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc,
    const std::vector< WString >& strings,
    int current_value,
    const std::string& default_value,
    int index /*= -1*/)
{
    const google::protobuf::Reflection* refl = message->GetReflection();

    WComboBox* combo_box = new WComboBox;
    WStringListModel* model = new WStringListModel(strings, this); 

    if(field_desc->has_default_value())
        model->insertString(0, "(default: " + default_value + ")");
    else
        model->insertString(0, "");

    combo_box->setModel(model);

    if((!field_desc->is_repeated() && refl->HasField(*message, field_desc)) ||
       (field_desc->is_repeated() && index < refl->FieldSize(*message, field_desc)))
        combo_box->setCurrentIndex(current_value+1);
    
    combo_box->changed().connect(
        boost::bind(&CommandContainer::handle_combo_field_changed,
                    this, message, field_desc, combo_box, index));

    return combo_box;
}

void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::dccl_default_value_field(
    WFormWidget*& value_field,
    const google::protobuf::FieldDescriptor* field_desc)
{

    const DCCLFieldOptions& options = field_desc->options().GetExtension(goby::field).dccl();

    if(options.has_min() && options.has_max())
    {
        WValidator* validator = value_field->validator();
        if(WIntValidator* int_validator = dynamic_cast<WIntValidator*>(validator))
            int_validator->setRange(options.min(), options.max());
        if(WDoubleValidator* double_validator = dynamic_cast<WDoubleValidator*>(validator))
            double_validator->setRange(options.min(), options.max());
    }

    if(options.has_static_value())
    {
        if(WLineEdit* line_edit = dynamic_cast<WLineEdit*>(value_field))
        {
            line_edit->setText(options.static_value());
            line_edit->changed().emit();
        }
        
        else if(WComboBox* combo_box = dynamic_cast<WComboBox*>(value_field))
        {
            combo_box->setCurrentIndex(combo_box->findText(options.static_value()));
            combo_box->changed().emit();
        }
        
        value_field->setDisabled(true);
    }

    if(options.has_max_length())
    {
        if(field_desc->type() == google::protobuf::FieldDescriptor::TYPE_STRING)
        {
            WLengthValidator* validator =
                new WLengthValidator(0, options.max_length());
            value_field->setValidator(validator);
        } 
        else if(field_desc->type() == google::protobuf::FieldDescriptor::TYPE_BYTES)
        {
            WRegExpValidator* validator =
                new WRegExpValidator("([0-9,a-f,A-F][0-9,a-f,A-F]){0," + goby::util::as<std::string>(options.max_length()) +"}");
                
            value_field->setValidator(validator);

        }
    }
}

void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::dccl_default_modify_field(
    WFormWidget*& modify_field,
    const google::protobuf::FieldDescriptor* field_desc)
{
    const DCCLFieldOptions& options = field_desc->options().GetExtension(goby::field).dccl();

    if(options.has_max_repeat())
    {
        if(WSpinBox* spin_box = dynamic_cast<WSpinBox*>(modify_field))
            spin_box->setMaximum(options.max_repeat());
    }
}


std::string goby::common::LiaisonCommander::ControlsContainer::CommandContainer::string_from_dccl_double(double* value, const google::protobuf::FieldDescriptor* field_desc)
{
    const DCCLFieldOptions& options = field_desc->options().GetExtension(goby::field).dccl();
    *value = goby::util::unbiased_round(*value, options.precision());

    if(options.precision() < 0)
    {
        return goby::util::as<std::string>(*value,
                                           std::max(0.0, std::log10(std::abs(*value)) + options.precision()),
                                           goby::util::FLOAT_SCIENTIFIC);
    }
    else
    {
        return goby::util::as<std::string>(*value,
                                           options.precision(),
                                           goby::util::FLOAT_FIXED); 
    }
}

void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_field_focus(int field_info_index)
{
    field_info_stack_->setCurrentIndex(field_info_index);
}



void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_repeated_size_change(
    int desired_size,
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc,
    WTreeTableNode* parent)
{
    const google::protobuf::Reflection* refl = message->GetReflection();
    
    // add nodes
    while(desired_size > static_cast<int>(parent->childNodes().size()))
    {
        int index = parent->childNodes().size();
        WTreeTableNode* node =
            new WTreeTableNode("index: " + goby::util::as<std::string>(index), 0, parent);

        
        if((parent->styleClass() == STRIPE_ODD_CLASS && index % 2) || (parent->styleClass() == STRIPE_EVEN_CLASS && !(index % 2)))
            node->setStyleClass(STRIPE_ODD_CLASS);
        else
            node->setStyleClass(STRIPE_EVEN_CLASS);

        WFormWidget* value_field = 0;

        if(field_desc->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            if(refl->FieldSize(*message, field_desc) <= index)
            {
                generate_tree(node, refl->AddMessage(message, field_desc));
            }
            else
            {
                parent->expand();
                generate_tree(node, refl->MutableRepeatedMessage(message, field_desc, index));
            }
        }
        else
        {
            generate_tree_field(value_field, message, field_desc, index);
        }
        
        if(value_field)
            node->setColumnWidget(1, value_field);
        parent->expand();
        node->expand();
    }

    // remove nodes
    while(desired_size < static_cast<int>(parent->childNodes().size()))
    {
        parent->removeChildNode(parent->childNodes().back());

        refl->RemoveLast(message, field_desc);
    }    
}


void goby::common::LiaisonCommander::ControlsContainer::CommandContainer::handle_toggle_single_message(
    const WMouseEvent& mouse,
    google::protobuf::Message* message,
    const google::protobuf::FieldDescriptor* field_desc,
    WPushButton* button,
    WTreeTableNode* parent)
{
    if(button->text() == MESSAGE_INCLUDE_TEXT)
    {
        generate_tree(parent,
                      message->GetReflection()->MutableMessage(message, field_desc));
        
        parent->expand();
        
        button->setText(MESSAGE_REMOVE_TEXT);
    }
    else
    {
        const std::vector< WTreeNode * > children = parent->childNodes();
        message->GetReflection()->ClearField(message, field_desc);
        for(int i = 0, n = children.size(); i < n; ++i)
            parent->removeChildNode(children[i]);        
        
        button->setText(MESSAGE_INCLUDE_TEXT);
    }
    
}
