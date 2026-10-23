foreground=true;created=0;alive=0;textWrites=0;objects={};maxStep=0
function hp_ui_dropdown(parent,options) local o=hp_ui_container(parent);o.options=options;o.value=0;return o end
function hp_ui_dropdown_set_options(o,options)o.options=options;o.value=0 end
function hp_ui_dropdown_set_value(o,value)o.value=value end
function hp_ui_dropdown_value(o)return o.value end
local root={id=0,children={}}
function hp_ui_assert_foreground() assert(foreground,'foreground required') end
function hp_ui_request_work() end
function hp_root() return root end
function hp_millis() return 0 end
local function create(parent,text)
 created=created+1;alive=alive+1
 local o={id=created,parent=parent,children={},text=text};objects[created]=o
 parent.children[#parent.children+1]=o;return o
end
for _,name in ipairs({'hp_ui_container','hp_ui_panel','hp_label','hp_button','hp_ui_textarea','hp_switch','hp_ui_progress','hp_ui_spinner','hp_ui_list','hp_ui_list_item','hp_ui_tabview','hp_ui_tab','hp_ui_chart'}) do _G[name]=create end
hp_ui_virtual_list=create
hp_ui_month_grid=create
function hp_ui_delete(o)
 assert(not o.deleted and #o.children==0,'delete must be postorder');o.deleted=true;alive=alive-1
 for i,c in ipairs(o.parent.children) do if c==o then table.remove(o.parent.children,i);break end end
end
function hp_ui_move(o,index)
 local list=o.parent.children;for i,c in ipairs(list) do if c==o then table.remove(list,i);break end end
 table.insert(list,math.min(index+1,#list+1),o)
end
function hp_on_click(o,name) o.callback=name end
function hp_on_value_changed(o,name) o.changed=name end
function hp_ui_style_input(o) o.inputStyled=true end
function hp_set_text(o,text) if o.text~=text then o.text=text;textWrites=textWrites+1 end end
hp_ui_button_text=hp_set_text;hp_ui_textarea_set=hp_set_text
function hp_ui_percent(x) return -x end
function hp_ui_content_size() return -999 end
for _,name in ipairs({'hp_ui_flex','hp_set_padding','hp_set_hidden','hp_set_enabled','hp_set_size','hp_ui_grow','hp_ui_gap','hp_ui_button_primary','hp_ui_text_style','hp_ui_progress_set','hp_set_checked'}) do _G[name]=function() end end
