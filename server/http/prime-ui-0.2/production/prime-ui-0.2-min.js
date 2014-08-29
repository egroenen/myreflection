/*
 * Copyright 2009-2012 Prime Teknoloji.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
PUI={scrollInView:function(b,e){var h=parseFloat(b.css("borderTopWidth"))||0,d=parseFloat(b.css("paddingTop"))||0,f=e.offset().top-b.offset().top-h-d,a=b.scrollTop(),c=b.height(),g=e.outerHeight(true);
if(f<0){b.scrollTop(a+f)
}else{if((f+g)>c){b.scrollTop(a+f-c+g)
}}}};$(function(){$.widget("prime-ui.puiaccordion",{options:{activeIndex:0,multiple:false},_create:function(){if(this.options.multiple){this.options.activeIndex=[]
}var a=this;
this.element.addClass("pui-accordion ui-widget ui-helper-reset");
this.element.children("h3").addClass("pui-accordion-header ui-helper-reset ui-state-default").each(function(c){var f=$(this),e=f.html(),d=(c==a.options.activeIndex)?"ui-state-active ui-corner-top":"ui-corner-all",b=(c==a.options.activeIndex)?"ui-icon ui-icon-triangle-1-s":"ui-icon ui-icon-triangle-1-e";
f.addClass(d).html('<span class="'+b+'"></span><a href="#">'+e+"</a>")
});
this.element.children("div").each(function(b){var c=$(this);
c.addClass("pui-accordion-content ui-helper-reset ui-widget-content");
if(b!=a.options.activeIndex){c.addClass("ui-helper-hidden")
}});
this.headers=this.element.children(".pui-accordion-header");
this.panels=this.element.children(".pui-accordion-content");
this.headers.children("a").disableSelection();
this._bindEvents()
},_bindEvents:function(){var a=this;
this.headers.mouseover(function(){var b=$(this);
if(!b.hasClass("ui-state-active")&&!b.hasClass("ui-state-disabled")){b.addClass("ui-state-hover")
}}).mouseout(function(){var b=$(this);
if(!b.hasClass("ui-state-active")&&!b.hasClass("ui-state-disabled")){b.removeClass("ui-state-hover")
}}).click(function(d){var c=$(this);
if(!c.hasClass("ui-state-disabled")){var b=c.index()/2;
if(c.hasClass("ui-state-active")){a.unselect(b)
}else{a.select(b)
}}d.preventDefault()
})
},select:function(b){var a=this.panels.eq(b);
this._trigger("change",a);
if(this.options.multiple){this._addToSelection(b)
}else{this.options.activeIndex=b
}this._show(a)
},unselect:function(b){var a=this.panels.eq(b),c=a.prev();
c.attr("aria-expanded",false).children(".ui-icon").removeClass("ui-icon-triangle-1-s").addClass("ui-icon-triangle-1-e");
c.removeClass("ui-state-active ui-corner-top").addClass("ui-corner-all");
a.attr("aria-hidden",true).slideUp();
this._removeFromSelection(b)
},_show:function(b){if(!this.options.multiple){var c=this.headers.filter(".ui-state-active");
c.children(".ui-icon").removeClass("ui-icon-triangle-1-s").addClass("ui-icon-triangle-1-e");
c.attr("aria-expanded",false).removeClass("ui-state-active ui-corner-top").addClass("ui-corner-all").next().attr("aria-hidden",true).slideUp()
}var a=b.prev();
a.attr("aria-expanded",true).addClass("ui-state-active ui-corner-top").removeClass("ui-state-hover ui-corner-all").children(".ui-icon").removeClass("ui-icon-triangle-1-e").addClass("ui-icon-triangle-1-s");
b.attr("aria-hidden",false).slideDown("normal")
},_addToSelection:function(a){this.options.activeIndex.push(a)
},_removeFromSelection:function(a){this.options.activeIndex=$.grep(this.options.activeIndex,function(b){return b!=a
})
}})
});$(function(){$.widget("prime-ui.puibutton",{options:{icon:null,iconPos:"left"},_create:function(){var b=this.element,d=b.text()||"pui-button",c=b.prop("disabled"),a=null;
if(this.options.icon){a=(d==="pui-button")?"pui-button-icon-only":"pui-button-text-icon-"+this.options.iconPos
}else{a="pui-button-text-only"
}if(c){a+=" ui-state-disabled"
}this.element.addClass("pui-button ui-widget ui-state-default ui-corner-all "+a).text("");
if(this.options.icon){this.element.append('<span class="pui-button-icon-'+this.options.iconPos+" ui-icon "+this.options.icon+'" />')
}this.element.append('<span class="pui-button-text">'+d+"</span>");
b.attr("role","button").attr("aria-disabled",c);
if(!c){this._bindEvents()
}},_bindEvents:function(){var a=this.element,b=this;
a.on("mouseover.puibutton",function(){if(!a.prop("disabled")){a.addClass("ui-state-hover")
}}).on("mouseout.puibutton",function(){$(this).removeClass("ui-state-active ui-state-hover")
}).on("mousedown.puibutton",function(){if(!a.hasClass("ui-state-disabled")){a.addClass("ui-state-active").removeClass("ui-state-hover")
}}).on("mouseup.puibutton",function(c){a.removeClass("ui-state-active").addClass("ui-state-hover");
b._trigger("click",c)
}).on("focus.puibutton",function(){a.addClass("ui-state-focus")
}).on("blur.puibutton",function(){a.removeClass("ui-state-focus")
}).on("keydown.puibutton",function(c){if(c.keyCode==$.ui.keyCode.SPACE||c.keyCode==$.ui.keyCode.ENTER||c.keyCode==$.ui.keyCode.NUMPAD_ENTER){a.addClass("ui-state-active")
}}).on("keyup.puibutton",function(){a.removeClass("ui-state-active")
});
return this
},_unbindEvents:function(){this.element.off("mouseover.puibutton mouseout.puibutton mousedown.puibutton mouseup.puibutton focus.puibutton blur.puibutton keydown.puibutton keyup.puibutton")
},disable:function(){this._unbindEvents();
this.element.addClass("ui-state-disabled")
},enable:function(){this._bindEvents();
this.element.removeClass("ui-state-disabled")
}})
});$(function(){$.widget("prime-ui.puigrowl",{options:{sticky:false,life:3000},_create:function(){var a=this.element;
a.addClass("pui-growl ui-widget").appendTo(document.body)
},show:function(a){var b=this;
this.clear();
$.each(a,function(c,d){b._renderMessage(d)
})
},clear:function(){this.element.children("div.pui-growl-item-container").remove()
},_renderMessage:function(c){var a='<div class="pui-growl-item-container ui-state-highlight ui-corner-all ui-helper-hidden pui-shadow" aria-live="polite">';
a+='<div class="pui-growl-item">';
a+='<div class="pui-growl-icon-close ui-icon ui-icon-closethick" style="display:none"></div>';
a+='<span class="pui-growl-image pui-growl-image-'+c.severity+'" />';
a+='<div class="pui-growl-message">';
a+='<span class="pui-growl-title">'+c.summary+"</span>";
a+="<p>"+c.detail+"</p>";
a+='</div><div style="clear: both;"></div></div></div>';
var b=$(a);
this._bindMessageEvents(b);
b.appendTo(this.element).fadeIn()
},_removeMessage:function(a){a.fadeTo("normal",0,function(){a.slideUp("normal","easeInOutCirc",function(){a.remove()
})
})
},_bindMessageEvents:function(a){var c=this,b=this.options.sticky;
a.on("mouseover.puigrowl",function(){var d=$(this);
if(!d.is(":animated")){d.find("div.pui-growl-icon-close:first").show()
}}).on("mouseout.puigrowl",function(){$(this).find("div.pui-growl-icon-close:first").hide()
});
a.find("div.pui-growl-icon-close").on("click.puigrowl",function(){c._removeMessage(a);
if(!b){clearTimeout(a.data("timeout"))
}});
if(!b){this._setRemovalTimeout(a)
}},_setRemovalTimeout:function(a){var c=this;
var b=setTimeout(function(){c._removeMessage(a)
},this.options.life);
a.data("timeout",b)
}})
});$(function(){$.widget("prime-ui.puiinputtext",{_create:function(){var a=this.element,b=a.prop("disabled");
a.addClass("pui-inputtext ui-widget ui-state-default ui-corner-all");
if(b){a.addClass("ui-state-disabled")
}else{a.hover(function(){a.toggleClass("ui-state-hover")
}).focus(function(){a.addClass("ui-state-focus")
}).blur(function(){a.removeClass("ui-state-focus")
})
}a.attr("role","textbox").attr("aria-disabled",b).attr("aria-readonly",a.prop("readonly")).attr("aria-multiline",a.is("textarea"))
},_destroy:function(){}})
});$(function(){$.widget("prime-ui.puiinputtextarea",{options:{autoResize:false,autoComplete:false,maxlength:null,counter:null,counterTemplate:"{0}",minQueryLength:3,queryDelay:700},_create:function(){var a=this;
this.element.puiinputtext();
if(this.options.autoResize){this.options.rowsDefault=this.element.attr("rows");
this.options.colsDefault=this.element.attr("cols");
this.element.addClass("pui-inputtextarea-resizable");
this.element.keyup(function(){a._resize()
}).focus(function(){a._resize()
}).blur(function(){a._resize()
})
}if(this.options.maxlength){this.element.keyup(function(d){var c=a.element.val(),b=c.length;
if(b>a.options.maxlength){a.element.val(c.substr(0,a.options.maxlength))
}if(a.options.counter){a._updateCounter()
}})
}if(this.options.counter){this._updateCounter()
}if(this.options.autoComplete){this._initAutoComplete()
}},_updateCounter:function(){var d=this.element.val(),c=d.length;
if(this.options.counter){var b=this.options.maxlength-c,a=this.options.counterTemplate.replace("{0}",b);
this.options.counter.text(a)
}},_resize:function(){var d=0,a=this.element.val().split("\n");
for(var b=a.length-1;
b>=0;
--b){d+=Math.floor((a[b].length/this.options.colsDefault)+1)
}var c=(d>=this.options.rowsDefault)?(d+1):this.options.rowsDefault;
this.element.attr("rows",c)
},_initAutoComplete:function(){var b='<div id="'+this.id+'_panel" class="pui-autocomplete-panel ui-widget-content ui-corner-all ui-helper-hidden ui-shadow"></div>',c=this;
this.panel=$(b).appendTo(document.body);
this.element.keyup(function(g){var f=$.ui.keyCode;
switch(g.which){case f.UP:case f.LEFT:case f.DOWN:case f.RIGHT:case f.ENTER:case f.NUMPAD_ENTER:case f.TAB:case f.SPACE:case f.CONTROL:case f.ALT:case f.ESCAPE:case 224:break;
default:var d=c._extractQuery();
if(d&&d.length>=c.options.minQueryLength){if(c.timeout){c._clearTimeout(c.timeout)
}c.timeout=setTimeout(function(){c.search(d)
},c.options.queryDelay)
}break
}}).keydown(function(j){var d=c.panel.is(":visible"),i=$.ui.keyCode;
switch(j.which){case i.UP:case i.LEFT:if(d){var h=c.items.filter(".ui-state-highlight"),g=h.length==0?c.items.eq(0):h.prev();
if(g.length==1){h.removeClass("ui-state-highlight");
g.addClass("ui-state-highlight");
if(c.options.scrollHeight){PUI.scrollInView(c.panel,g)
}}j.preventDefault()
}else{c._clearTimeout()
}break;
case i.DOWN:case i.RIGHT:if(d){var h=c.items.filter(".ui-state-highlight"),f=h.length==0?_self.items.eq(0):h.next();
if(f.length==1){h.removeClass("ui-state-highlight");
f.addClass("ui-state-highlight");
if(c.options.scrollHeight){PUI.scrollInView(c.panel,f)
}}j.preventDefault()
}else{c._clearTimeout()
}break;
case i.ENTER:case i.NUMPAD_ENTER:if(d){c.items.filter(".ui-state-highlight").trigger("click");
j.preventDefault()
}else{c._clearTimeout()
}break;
case i.SPACE:case i.CONTROL:case i.ALT:case i.BACKSPACE:case i.ESCAPE:case 224:c._clearTimeout();
if(d){c._hide()
}break;
case i.TAB:c._clearTimeout();
if(d){c.items.filter(".ui-state-highlight").trigger("click");
c._hide()
}break
}});
$(document.body).bind("mousedown.puiinputtextarea",function(d){if(c.panel.is(":hidden")){return
}var f=c.panel.offset();
if(d.target===c.element.get(0)){return
}if(d.pageX<f.left||d.pageX>f.left+c.panel.width()||d.pageY<f.top||d.pageY>f.top+c.panel.height()){c._hide()
}});
var a="resize."+this.id;
$(window).unbind(a).bind(a,function(){if(c.panel.is(":visible")){c._hide()
}})
},_bindDynamicEvents:function(){var a=this;
this.items.bind("mouseover",function(){var b=$(this);
if(!b.hasClass("ui-state-highlight")){a.items.filter(".ui-state-highlight").removeClass("ui-state-highlight");
b.addClass("ui-state-highlight")
}}).bind("click",function(d){var c=$(this),e=c.attr("data-item-value"),b=e.substring(a.query.length);
a.element.focus();
a.element.insertText(b,a.element.getSelection().start,true);
a._hide();
a._trigger("itemselect",d,c)
})
},_clearTimeout:function(){if(this.timeout){clearTimeout(this.timeout)
}this.timeout=null
},_extractQuery:function(){var b=this.element.getSelection().end,a=/\S+$/.exec(this.element.get(0).value.slice(0,b)),c=a?a[0]:null;
return c
},search:function(b){this.query=b;
var a={query:b};
if(this.options.completeSource){this.options.completeSource.call(this,a,this._handleResponse)
}},_handleResponse:function(c){this.panel.html("");
var d=$('<ul class="pui-autocomplete-items pui-autocomplete-list ui-widget-content ui-widget ui-corner-all ui-helper-reset"></ul>');
for(var a=0;
a<c.length;
a++){var b=$('<li class="pui-autocomplete-item pui-autocomplete-list-item ui-corner-all"></li>');
b.attr("data-item-value",c[a].value);
b.text(c[a].label);
d.append(b)
}this.panel.append(d).show();
this.items=this.panel.find(".pui-autocomplete-item");
this._bindDynamicEvents();
if(this.items.length>0){this.items.eq(0).addClass("ui-state-highlight");
if(this.options.scrollHeight&&this.panel.height()>this.options.scrollHeight){this.panel.height(this.options.scrollHeight)
}if(this.panel.is(":hidden")){this._show()
}else{this._alignPanel()
}}else{this.panel.hide()
}},_alignPanel:function(){var b=this.element.getCaretPosition(),a=this.element.offset();
this.panel.css({left:a.left+b.left,top:a.top+b.top,width:this.element.innerWidth()})
},_show:function(){this._alignPanel();
this.panel.show()
},_hide:function(){this.panel.hide()
},_refresh:function(){},_destroy:function(){alert("destroy")
}})
});$(function(){$.widget("prime-ui.puirating",{options:{stars:5,cancel:true},_create:function(){var b=this.element;
b.wrap("<div />");
this.container=b.parent();
this.container.addClass("pui-rating");
var d=b.val(),e=d==""?null:parseInt(d);
if(this.options.cancel){this.container.append('<div class="pui-rating-cancel"><a></a></div>')
}for(var c=0;
c<this.options.stars;
c++){var a=(e>c)?"pui-rating-star pui-rating-star-on":"pui-rating-star";
this.container.append('<div class="'+a+'"><a></a></div>')
}this.stars=this.container.children(".pui-rating-star");
if(b.prop("disabled")){this.container.addClass("ui-state-disabled")
}else{if(!b.prop("readonly")){this._bindEvents()
}}},_bindEvents:function(){var a=this;
this.stars.click(function(){var b=a.stars.index(this)+1;
a._setValue(b)
});
this.container.children(".pui-rating-cancel").hover(function(){$(this).toggleClass("pui-rating-cancel-hover")
}).click(function(){a.cancel()
})
},cancel:function(){this.element.val("");
this.stars.filter(".pui-rating-star-on").removeClass("pui-rating-star-on")
},_getValue:function(){var a=this.element.val();
return a==""?null:parseInt(a)
},_setValue:function(b){this.element.val(b);
this.stars.removeClass("pui-rating-star-on");
for(var a=0;
a<b;
a++){this.stars.eq(a).addClass("pui-rating-star-on")
}this._trigger("rate",null,b)
}})
});$(function(){$.widget("prime-ui.puispinner",{options:{step:1},_create:function(){var a=this.element,b=a.prop("disabled");
a.puiinputtext().addClass("pui-spinner-input").wrap('<span class="pui-spinner ui-widget ui-corner-all" />');
this.wrapper=a.parent();
this.wrapper.append('<a class="pui-spinner-button pui-spinner-up ui-corner-tr ui-button ui-widget ui-state-default ui-button-text-only"><span class="ui-button-text"><span class="ui-icon ui-icon-triangle-1-n"></span></span></a><a class="pui-spinner-button pui-spinner-down ui-corner-br ui-button ui-widget ui-state-default ui-button-text-only"><span class="ui-button-text"><span class="ui-icon ui-icon-triangle-1-s"></span></span></a>');
this.upButton=this.wrapper.children("a.pui-spinner-up");
this.downButton=this.wrapper.children("a.pui-spinner-down");
this._initValue();
if(!b&&!a.prop("readonly")){this._bindEvents()
}if(b){this.wrapper.addClass("ui-state-disabled")
}a.attr({role:"spinner","aria-multiline":false,"aria-valuenow":this.value});
if(this.options.min!=undefined){a.attr("aria-valuemin",this.options.min)
}if(this.options.max!=undefined){a.attr("aria-valuemax",this.options.max)
}if(a.prop("disabled")){a.attr("aria-disabled",true)
}if(a.prop("readonly")){a.attr("aria-readonly",true)
}},_bindEvents:function(){var a=this;
this.wrapper.children(".pui-spinner-button").mouseover(function(){$(this).addClass("ui-state-hover")
}).mouseout(function(){$(this).removeClass("ui-state-hover ui-state-active");
if(a.timer){clearInterval(a.timer)
}}).mouseup(function(){clearInterval(a.timer);
$(this).removeClass("ui-state-active").addClass("ui-state-hover")
}).mousedown(function(d){var c=$(this),b=c.hasClass("pui-spinner-up")?1:-1;
c.removeClass("ui-state-hover").addClass("ui-state-active");
if(a.element.is(":not(:focus)")){a.element.focus()
}a._repeat(null,b);
d.preventDefault()
});
this.element.keydown(function(c){var b=$.ui.keyCode;
switch(c.which){case b.UP:a._spin(a.options.step);
break;
case b.DOWN:a._spin(-1*a.options.step);
break;
default:break
}}).keyup(function(){a._updateValue()
}).blur(function(){a._format()
}).focus(function(){a.element.val(a.value)
});
this.element.bind("mousewheel",function(b,c){if(a.element.is(":focus")){if(c>0){a._spin(a.options.step)
}else{a._spin(-1*a.options.step)
}return false
}})
},_repeat:function(a,b){var d=this,c=a||500;
clearTimeout(this.timer);
this.timer=setTimeout(function(){d._repeat(40,b)
},c);
this._spin(this.options.step*b)
},_spin:function(a){var b=this.value+a;
if(this.options.min!=undefined&&b<this.options.min){b=this.cfg.min
}if(this.options.max!=undefined&&b>this.options.max){b=this.cfg.max
}this.element.val(b).attr("aria-valuenow",b);
this.value=b;
this.element.trigger("change")
},_updateValue:function(){var a=this.element.val();
if(a==""){if(this.options.min!=undefined){this.value=this.options.min
}else{this.value=0
}}else{if(this.options.step){a=parseFloat(a)
}else{a=parseInt(a)
}if(!isNaN(a)){this.value=a
}}},_initValue:function(){var a=this.element.val();
if(a==""){if(this.options.min!=undefined){this.value=this.options.min
}else{this.value=0
}}else{if(this.options.prefix){a=a.split(this.options.prefix)[1]
}if(this.options.suffix){a=a.split(this.options.suffix)[0]
}if(this.options.step){this.value=parseFloat(a)
}else{this.value=parseInt(a)
}}},_format:function(){var a=this.value;
if(this.options.prefix){a=this.options.prefix+a
}if(this.options.suffix){a=a+this.options.suffix
}this.element.val(a)
}})
});$(function(){$.widget("prime-ui.puitabview",{options:{activeIndex:0,orientation:"top"},_create:function(){var a=this.element;
a.addClass("pui-tabview ui-widget ui-widget-content ui-corner-all ui-hidden-container").children("ul").addClass("pui-tabview-nav ui-helper-reset ui-helper-clearfix ui-widget-header ui-corner-all").children("li").addClass("ui-state-default ui-corner-top");
a.addClass("pui-tabview-"+this.options.orientation);
a.children("div").addClass("pui-tabview-panels").children().addClass("pui-tabview-panel ui-widget-content ui-corner-bottom");
a.find("> ul.pui-tabview-nav > li").eq(this.options.activeIndex).addClass("pui-tabview-selected ui-state-active");
a.find("> div.pui-tabview-panels > div.pui-tabview-panel:not(:eq("+this.options.activeIndex+"))").addClass("ui-helper-hidden");
this.navContainer=a.children(".pui-tabview-nav");
this.panelContainer=a.children(".pui-tabview-panels");
this._bindEvents()
},_bindEvents:function(){var a=this;
this.navContainer.children("li").bind("mouseover.tabview",function(c){var b=$(this);
if(!b.hasClass("ui-state-disabled")){b.addClass("ui-state-hover")
}}).bind("mouseout.tabview",function(c){var b=$(this);
if(!b.hasClass("ui-state-disabled")){b.removeClass("ui-state-hover")
}}).bind("click.tabview",function(d){var c=$(this);
if($(d.target).is(":not(.ui-icon-close)")){var b=c.index();
if(!c.hasClass("ui-state-disabled")&&b!=a.options.selected){a.select(b)
}}d.preventDefault()
});
this.navContainer.find("li .ui-icon-close").bind("click.tabview",function(c){var b=$(this).parent().index();
a.remove(b);
c.preventDefault()
})
},select:function(c){this.options.selected=c;
var b=this.panelContainer.children().eq(c),g=this.navContainer.children(),f=g.filter(".ui-state-active"),a=g.eq(b.index()),e=this.panelContainer.children(".pui-tabview-panel:visible"),d=this;
e.attr("aria-hidden",true);
f.attr("aria-expanded",false);
b.attr("aria-hidden",false);
a.attr("aria-expanded",true);
if(this.options.effect){e.hide(this.options.effect.name,null,this.options.effect.duration,function(){f.removeClass("ui-state-focus pui-tabview-selected ui-state-active");
a.addClass("ui-state-focus pui-tabview-selected ui-state-active");
b.show(d.options.name,null,d.options.effect.duration,function(){d._trigger("change",null,c)
})
})
}else{f.removeClass("ui-state-focus pui-tabview-selected ui-state-active");
e.hide();
a.addClass("ui-state-focus pui-tabview-selected ui-state-active");
b.show();
this._trigger("change",null,c)
}},remove:function(b){var d=this.navContainer.children().eq(b),a=this.panelContainer.children().eq(b);
this._trigger("close",null,b);
d.remove();
a.remove();
if(b==this.options.selected){var c=this.options.selected==this.getLength()?this.options.selected-1:this.options.selected;
this.select(c)
}},getLength:function(){return this.navContainer.children().length
},getActiveIndex:function(){return this.options.selected
},_markAsLoaded:function(a){a.data("loaded",true)
},_isLoaded:function(a){return a.data("loaded")==true
},disable:function(a){this.navContainer.children().eq(a).addClass("ui-state-disabled")
},enable:function(a){this.navContainer.children().eq(a).removeClass("ui-state-disabled")
}})
});