/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true */
/*global Backbone, $, window, setTimeout, Joi, _ */
/*global templateEngine*/

(function () {
  "use strict";

  var createButtonStub = function(type, title, cb) {
    return {
      type: type,
      title: title,
      callback: cb
    };
  };

  var createTextStub = function(type, label, value, info, placeholder, mandatory, regexp,
                                addDelete, addAdd, maxEntrySize, tags) {
    var obj = {
      type: type,
      label: label
    };
    if (value) {
      obj.value = value;
    }
    if (info) {
      obj.info = info;
    }
    if (placeholder) {
      obj.placeholder = placeholder;
    }
    if (mandatory) {
      obj.mandatory = mandatory;
    }
    if (addDelete !== undefined) {
      obj.addDelete = addDelete;
    }
    if (addAdd !== undefined) {
      obj.addAdd = addAdd;
    }
    if (maxEntrySize !== undefined) {
      obj.maxEntrySize = maxEntrySize;
    }
    if (tags !== undefined) {
      obj.tags = tags;
    }
    if (regexp){
      // returns true if the string contains the match
      obj.validateInput = function(el){
        //return regexp.test(el.val());
        return regexp;
      };
    }
    return obj;
  };

  window.ModalView = Backbone.View.extend({

    baseTemplate: templateEngine.createTemplate("modalBase.ejs"),
    tableTemplate: templateEngine.createTemplate("modalTable.ejs"),
    el: "#modalPlaceholder",
    contentEl: "#modalContent",
    hideFooter: false,
    confirm: {
      list: "#modal-delete-confirmation",
      yes: "#modal-confirm-delete",
      no: "#modal-abort-delete"
    },
    enabledHotkey: false,
    enableHotKeys : true,

    buttons: {
      SUCCESS: "success",
      NOTIFICATION: "notification",
      DELETE: "danger",
      NEUTRAL: "neutral",
      CLOSE: "close"
    },
    tables: {
      READONLY: "readonly",
      TEXT: "text",
      PASSWORD: "password",
      SELECT: "select",
      SELECT2: "select2",
      CHECKBOX: "checkbox"
    },
    closeButton: {
      type: "close",
      title: "Cancel"
    },

    initialize: function() {
      Object.freeze(this.buttons);
      Object.freeze(this.tables);
      var self = this;
      this.closeButton.callback = function() {
        self.hide();
      };
    },

    createModalHotkeys: function() {
      //submit modal
      $(this.el).bind('keydown', 'ctrl+return', function(){
        $('.button-success').click();
      });
      $("input", $(this.el)).bind('keydown', 'return', function(){
        $('.button-success').click();
      });
      $("select", $(this.el)).bind('keydown', 'return', function(){
        $('.button-success').click();
      });
    },

    createInitModalHotkeys: function() {
      var self = this;
      //navigate through modal buttons
      //left cursor
      $(this.el).bind('keydown', 'left', function(){
        self.navigateThroughButtons('left');
      });
      //right cursor
      $(this.el).bind('keydown', 'right', function(){
        self.navigateThroughButtons('right');
      });

    },

    navigateThroughButtons: function(direction) {
      var hasFocus = $('.modal-footer button').is(':focus');
      if (hasFocus === false) {
        if (direction === 'left') {
          $('.modal-footer button').first().focus();
        }
        else if (direction === 'right') {
          $('.modal-footer button').last().focus();
        }
      }
      else if (hasFocus === true) {
        if (direction === 'left') {
          $(':focus').prev().focus();
        }
        else if (direction === 'right') {
          $(':focus').next().focus();
        }
      }

    },

    createCloseButton: function(cb) {
      var self = this;
      return createButtonStub(this.buttons.CLOSE, this.closeButton.title, function () {
          self.closeButton.callback();
          cb();
      });
    },

    createSuccessButton: function(title, cb) {
      return createButtonStub(this.buttons.SUCCESS, title, cb);
    },

    createNotificationButton: function(title, cb) {
      return createButtonStub(this.buttons.NOTIFICATION, title, cb);
    },

    createDeleteButton: function(title, cb) {
      return createButtonStub(this.buttons.DELETE, title, cb);
    },

    createNeutralButton: function(title, cb) {
      return createButtonStub(this.buttons.NEUTRAL, title, cb);
    },

    createDisabledButton: function(title) {
      var disabledButton = createButtonStub(this.buttons.NEUTRAL, title);
      disabledButton.disabled = true;
      return disabledButton;
    },

    createReadOnlyEntry: function(id, label, value, info, addDelete, addAdd) {
      var obj = createTextStub(this.tables.READONLY, label, value, info,undefined, undefined,
        undefined,addDelete, addAdd);
      obj.id = id;
      return obj;
    },

    createTextEntry: function(id, label, value, info, placeholder, mandatory, regexp) {
      var obj = createTextStub(this.tables.TEXT, label, value, info, placeholder, mandatory,
                               regexp);
      obj.id = id;
      return obj;
    },

    createSelect2Entry: function(
      id, label, value, info, placeholder, mandatory, addDelete, addAdd, maxEntrySize, tags) {
      var obj = createTextStub(this.tables.SELECT2, label, value, info, placeholder,
        mandatory, undefined, addDelete, addAdd, maxEntrySize, tags);
      obj.id = id;
      return obj;
    },

    createPasswordEntry: function(id, label, value, info, placeholder, mandatory) {
      var obj = createTextStub(this.tables.PASSWORD, label, value, info, placeholder, mandatory);
      obj.id = id;
      return obj;
    },

    createCheckboxEntry: function(id, label, value, info, checked) {
      var obj = createTextStub(this.tables.CHECKBOX, label, value, info);
      obj.id = id;
      if (checked) {
        obj.checked = checked;
      }
      return obj;
    },

    createSelectEntry: function(id, label, selected, info, options) {
      var obj = createTextStub(this.tables.SELECT, label, null, info);
      obj.id = id;
      if (selected) {
        obj.selected = selected;
      }
      obj.options = options;
      return obj;
    },

    createOptionEntry: function(label, value) {
      return {
        label: label,
        value: value || label
      };
    },

    show: function(templateName, title, buttons, tableContent, advancedContent,
        events) {
      var self = this, lastBtn, closeButtonFound = false;
      buttons = buttons || [];
      // Insert close as second from right
      if (buttons.length > 0) {
        buttons.forEach(function (b) {
            if (b.type === self.buttons.CLOSE) {
                closeButtonFound = true;
            }
        });
        if (!closeButtonFound) {
            lastBtn = buttons.pop();
            buttons.push(self.closeButton);
            buttons.push(lastBtn);
        }
      } else {
        buttons.push(this.closeButton);
      }
      $(this.el).html(this.baseTemplate.render({
        title: title,
        buttons: buttons,
        hideFooter: this.hideFooter
      }));
      _.each(buttons, function(b, i) {
        if (b.disabled || !b.callback) {
          return;
        }
        if (b.type === self.buttons.DELETE) {
          $("#modalButton" + i).bind("click", function() {
            $(self.confirm.yes).unbind("click");
            $(self.confirm.yes).bind("click", b.callback);
            $(self.confirm.list).css("display", "block");
          });
          return;
        }
        $("#modalButton" + i).bind("click", b.callback);
      });
      $(this.confirm.no).bind("click", function() {
        $(self.confirm.list).css("display", "none");
      });

      var template = templateEngine.createTemplate(templateName),
        model = {};
      model.content = tableContent || [];
      model.advancedContent = advancedContent || false;
      $(".modal-body").html(template.render(model));
      $('.modalTooltips').tooltip({
        position: {
          my: "left top",
          at: "right+55 top-1"
        }
      });
      var ind = buttons.indexOf(this.closeButton);
      buttons.splice(ind, 1);

      var completeTableContent = tableContent;
      try {
        _.each(advancedContent.content, function(x) {
          completeTableContent.push(x);
        });
      }
      catch(ignore) {
      }

      //handle select2
      _.each(completeTableContent, function(r) {
        if (r.type === self.tables.SELECT2) {
          $('#'+r.id).select2({
            tags: r.tags || [],
            showSearchBox: false,
            minimumResultsForSearch: -1,
            width: "336px",
            maximumSelectionSize: r.maxEntrySize || 8
          });
        }
      });//handle select2

      self.testInput = (function(){
        _.each(completeTableContent,function(r){

          if(r.validateInput) {
            //catch result of validation and act
            $('#' + r.id).on('keyup', function(){

              var validation = r.validateInput($('#' + r.id));
              var error = false, msg;

              _.each(validation, function(validator, key) {

                var schema = Joi.object().keys({
                  toCheck: validator.rule
                });

                Joi.validate({
                  toCheck: $('#' + r.id).val()
                },
                schema,
                function (err, value) {

                  if (err) {
                    msg = validator.msg;
                    error = true;
                  }
                });
              });
              var errorElement = $('#'+r.id).next()[0];

              if(error === true){
                // if validation throws an error
                $('#' + r.id).addClass('invalid-input');
                $('.modal-footer .button-success').prop('disabled', true);
                $('.modal-footer .button-success').addClass('disabled');

                if (errorElement) {
                  //error element available
                  $(errorElement).text(msg);
                }
                else {
                  //error element not available
                  $('#' + r.id).after('<p class="errorMessage">' + msg+ '</p>');
                }

              }
              else {
                //validation throws success
                $('#' + r.id).removeClass('invalid-input');
                $('.modal-footer .button-success').prop('disabled', false);
                $('.modal-footer .button-success').removeClass('disabled');
                if (errorElement) {
                  $(errorElement).remove();
                }
              }
            });
          }
        });
      }());
      if (events) {
          this.events = events;
          this.delegateEvents();
      }

      $("#modal-dialog").modal("show");

      //enable modal hotkeys after rendering is complete
      if (this.enabledHotkey === false) {
        this.createInitModalHotkeys();
        this.enabledHotkey = true;
      }
      if (this.enableHotKeys) {
        this.createModalHotkeys();
      }

      //if input-field is available -> autofocus first one
      var focus = $('#modal-dialog').find('input');
      if (focus) {
        setTimeout(function() {$(focus[0]).focus();}, 800);
      }

    },

    hide: function() {
      $("#modal-dialog").modal("hide");
    }
  });
}());
