////===================================================================
 //
 // ob_table_engine.h / hash / common / Oceanbase
 //
 // Copyright (C) 2010, 2011, 2012 Taobao.com, Inc.
 //
 // Created on 2012-06-20 by Yubai (yubai.lk@taobao.com)
 //
 // -------------------------------------------------------------------
 //
 // Description
 //
 //
 // -------------------------------------------------------------------
 //
 // Change Log
 //
////====================================================================

#include "ob_update_server_main.h"
#include "ob_table_engine.h"
#include "ob_memtable.h"
#include "ob_sessionctx_factory.h"

namespace oceanbase
{
  using namespace common;
  namespace updateserver
  {
    std::pair<int64_t, int64_t> ObCellInfoNode::get_size_and_cnt() const
    {
      std::pair<int64_t, int64_t> ret(0, 0);
      ObCellInfoNodeIterable cci;
      uint64_t column_id = OB_INVALID_ID;
      ObObj value;
      cci.set_cell_info_node(this);
      while (OB_SUCCESS == cci.next_cell())
      {
        if (OB_SUCCESS == cci.get_cell(column_id, value))
        {
          ret.first += 1;
          ret.second += MemTable::get_varchar_length_kb_(value);
        }
      }
      return ret;
    }

    ObCellInfoNodeIterable::ObCellInfoNodeIterable() : cell_info_node_(NULL),
                                                       is_iter_end_(false),
                                                       cci_(),
                                                       ctrl_list_(NULL)
    {
      head_node_.column_id = OB_INVALID_ID;
      head_node_.next = NULL;

      rne_node_.column_id = OB_INVALID_ID;
      rne_node_.value.set_ext(ObActionFlag::OP_ROW_DOES_NOT_EXIST);
      rne_node_.next = NULL;

      mtime_node_.column_id = OB_INVALID_ID;
      mtime_node_.next = NULL;
    }

    ObCellInfoNodeIterable::~ObCellInfoNodeIterable()
    {
    }

    int ObCellInfoNodeIterable::next_cell()
    {
      int ret = OB_SUCCESS;
      if (is_iter_end_
          || (NULL == cell_info_node_ && NULL == ctrl_list_))
      {
        ret = OB_ITER_END;
      }
      else
      {
        if (NULL == ctrl_list_
            || NULL == (ctrl_list_ = ctrl_list_->next))
        {
          if (NULL == cell_info_node_)
          {
            ret = OB_ITER_END;
          }
          else
          {
            ret = cci_.next_cell();
            bool is_row_finished = false;
            if (OB_SUCCESS == ret)
            {
              uint64_t column_id = OB_INVALID_ID;
              ObObj value;
              ret = cci_.get_cell(column_id, value, &is_row_finished);
            }
            if (OB_SUCCESS == ret
                && is_row_finished)
            {
              ret = OB_ITER_END;
            }
          }
        }
      }
      is_iter_end_ = (OB_ITER_END == ret);
      return ret;
    }

    int ObCellInfoNodeIterable::get_cell(uint64_t &column_id, ObObj &value)
    {
      int ret = OB_SUCCESS;
      if (is_iter_end_
          || (NULL == cell_info_node_ && NULL == ctrl_list_)
          || &head_node_ == ctrl_list_)
      {
        ret = OB_ITER_END;
      }
      else if (NULL != ctrl_list_)
      {
        column_id = ctrl_list_->column_id;
        value = ctrl_list_->value;
      }
      else
      {
        bool is_row_finished = false;
        ret = cci_.get_cell(column_id, value, &is_row_finished);
      }
      return ret;
    }

    void ObCellInfoNodeIterable::set_mtime(const uint64_t column_id, const ObModifyTime value)
    {
      mtime_node_.column_id = column_id;
      mtime_node_.value.set_modifytime(value);
      mtime_node_.next = ctrl_list_;
      ctrl_list_ = &mtime_node_;
    }

    void ObCellInfoNodeIterable::set_rne()
    {
      rne_node_.next = ctrl_list_;
      ctrl_list_ = &rne_node_;
    }

    void ObCellInfoNodeIterable::set_head()
    {
      head_node_.next = ctrl_list_;
      ctrl_list_ = &head_node_;
    }

    void ObCellInfoNodeIterable::reset()
    {
      cell_info_node_ = NULL;
      is_iter_end_ = false;
      //cci_.init(NULL);
      ctrl_list_ = NULL;
    }

    void ObCellInfoNodeIterable::set_cell_info_node(const ObCellInfoNode *cell_info_node)
    {
      cell_info_node_ = cell_info_node;
      is_iter_end_ = false;
      //cci_.init(NULL == cell_info_node_ ? NULL : cell_info_node_->buf);
      if (NULL != cell_info_node_)
      {
        cci_.init(cell_info_node_->buf);
      }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    ObCellInfoNodeIterableWithCTime::ObCellInfoNodeIterableWithCTime() : inner_err(OB_SUCCESS),
                                                                         is_iter_end_(false),
                                                                         ext_list_(NULL),
                                                                         list_iter_(NULL),
                                                                         column_id_(OB_INVALID_ID),
                                                                         value_()
    {
      ctime_node_.column_id = OB_INVALID_ID;
      ctime_node_.next = NULL;

      for (int64_t i = 0; i < OB_MAX_ROWKEY_COLUMN_NUMBER; i++)
      {
        rowkey_node_[i].column_id = OB_INVALID_ID;
        rowkey_node_[i].next = NULL;
      }

      nop_node_.column_id = OB_INVALID_ID;
      nop_node_.value.set_ext(ObActionFlag::OP_NOP);
      nop_node_.next = NULL;
    }

    ObCellInfoNodeIterableWithCTime::~ObCellInfoNodeIterableWithCTime()
    {
    }

    int ObCellInfoNodeIterableWithCTime::next_cell()
    {
      int ret = OB_SUCCESS;
      if (OB_SUCCESS != inner_err)
      {
        ret = inner_err;
      }
      else if (is_iter_end_)
      {
        ret = OB_ITER_END;
      }
      else if (NULL != list_iter_
              && NULL != (list_iter_ = list_iter_->next))
      {
        column_id_ = list_iter_->column_id;
        value_ = list_iter_->value;
      }
      else if (ObModifyTimeType == value_.get_type())
      {
        int64_t vtime = 0;
        value_.get_modifytime(vtime);
        ctime_node_.value.set_createtime(vtime);
        list_iter_ = ext_list_;

        column_id_ = list_iter_->column_id;
        value_ = list_iter_->value;
      }
      else
      {
        ret = ObCellInfoNodeIterable::next_cell();
        if (OB_SUCCESS != ret
            || OB_SUCCESS != (ret = ObCellInfoNodeIterable::get_cell(column_id_, value_)))
        {
          is_iter_end_ = true;
        }
      }
      return ret;
    }

    int ObCellInfoNodeIterableWithCTime::get_cell(uint64_t &column_id, common::ObObj &value)
    {
      int ret = OB_SUCCESS;
      if (OB_SUCCESS != inner_err)
      {
        ret = inner_err;
      }
      else if (is_iter_end_)
      {
        ret = OB_ITER_END;
      }
      else
      {
        column_id = column_id_;
        value = value_;
      }
      return ret;
    }

    void ObCellInfoNodeIterableWithCTime::reset()
    {
      ObCellInfoNodeIterable::reset();
      inner_err = OB_SUCCESS;
      is_iter_end_ = false;
      ext_list_ = NULL;
      list_iter_ = NULL;
      column_id_ = OB_INVALID_ID;
      value_.set_null();
    }

    void ObCellInfoNodeIterableWithCTime::set_ctime_column_id(const uint64_t column_id)
    {
      ctime_node_.column_id = column_id;
      ctime_node_.next = ext_list_;
      ext_list_ = &ctime_node_;
    }

    void ObCellInfoNodeIterableWithCTime::set_rowkey_column(const uint64_t table_id, const ObRowkey &row_key)
    {
      const ObRowkeyInfo *rki = get_rowkey_info(table_id);
      if (NULL == rki
          || NULL == row_key.ptr())
      {
        //TBSYS_LOG(WARN, "get rowkey info fail table_id=%lu rowkey_info=%p rowkey=%p %s", table_id, rki, row_key.ptr(), to_cstring(row_key));
        inner_err = OB_SCHEMA_ERROR;
      }
      else
      {
        for (int64_t i = row_key.length() - 1; i >= 0; i--)
        {
          if (OB_SUCCESS == rki->get_column_id(i, rowkey_node_[i].column_id))
          {
            rowkey_node_[i].value = row_key.ptr()[i];
            rowkey_node_[i].next = ext_list_;
            ext_list_ = &(rowkey_node_[i]);
          }
          else
          {
            TBSYS_LOG(WARN, "get rowkey column id fail index=%ld table_id=%lu", i, table_id);
            inner_err = OB_SCHEMA_ERROR;
          }
        }
      }
    }

    void ObCellInfoNodeIterableWithCTime::set_nop()
    {
      nop_node_.next = ext_list_;
      ext_list_ = &nop_node_;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    int TEValueSessionCallback::cb_func(const bool rollback, void *data, BaseSessionCtx &session)
    {
      int ret = OB_SUCCESS;
      TEValueUCInfo *uci = (TEValueUCInfo*)data;
      if (NULL == uci
          || NULL == uci->value)
      {
        ret = OB_INVALID_ARGUMENT;
      }
      else if (!rollback)
      {
        ObCellInfoNode *iter = uci->uc_list_head;
        while (NULL != iter)
        {
          iter->modify_time = session.get_trans_id();
          TBSYS_LOG(DEBUG, "set value=%p node=%p modify_time=%ld", uci->value, iter, iter->modify_time);
          if (uci->uc_list_tail == iter)
          {
            break;
          }
          else
          {
            iter = iter->next;
          }
        }

        if (NULL == uci->value->list_tail)
        {
          uci->value->list_head = uci->uc_list_head;
        }
        else
        {
          uci->value->list_tail->next = uci->uc_list_head;
        }
        uci->value->list_tail = uci->uc_list_tail;
        TBSYS_LOG(DEBUG, "commit value=%p %s "
                  "cur_uc_info=%p uc_list_head=%p uc_list_tail=%p "
                  "trans_id=%ld session=%p sd=%u",
                  uci->value, uci->value->log_list(),
                  uci->value->cur_uc_info, uci->uc_list_head, uci->uc_list_tail,
                  session.get_trans_id(), &session, session.get_session_descriptor());
        uci->value->cell_info_cnt = (int16_t)(uci->value->cell_info_cnt + uci->uc_cell_info_cnt);
        uci->value->cell_info_size = (int16_t)(uci->value->cell_info_size + uci->uc_cell_info_size);
        uci->value->cur_uc_info = NULL;
      }
      else
      {
        TBSYS_LOG(DEBUG, "rollback value=%p %s "
                  "cur_uc_info=%p uc_list_head=%p uc_list_tail=%p "
                  "session=%p sd=%u",
                  uci->value, uci->value->log_list(),
                  uci->value->cur_uc_info, uci->uc_list_head, uci->uc_list_tail,
                  &session, session.get_session_descriptor());
        if (NULL != uci->value->list_tail)
        {
          uci->value->list_tail->next = NULL;
        }
        uci->value->cur_uc_info = NULL;
        // maybe free uncommited memory
      }
      return ret;
    }

    int TransSessionCallback::cb_func(const bool rollback, void *data, BaseSessionCtx &session)
    {
      UNUSED(session);
      int ret = OB_SUCCESS;
      TransUCInfo *uci = (TransUCInfo*)data;
      if (NULL == uci)
      {
        ret = OB_INVALID_ARGUMENT;
      }
      else if (!rollback)
      {
        if (NULL != uci->host)
        {
          uci->host->add_row_counter(uci->uc_row_counter);
          uci->host->update_checksum(uci->uc_checksum);
          uci->host->update_last_trans_id(session.get_trans_id());
        }
      }
      else
      {
        // do nothing
      }
      return ret;
    }
  }
}
