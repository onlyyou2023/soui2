#include "souistd.h"
#include "control/SMCListView.h"
#include "helper/SListViewItemLocator.h"

#pragma warning(disable : 4267 4018)

#define ITEM_MARGIN 4
namespace SOUI
{
    class SMCListViewDataSetObserver : public TObjRefImpl<IDataSetObserver>
    {
    public:
        SMCListViewDataSetObserver(SMCListView *pView):m_pOwner(pView)
        {
        }
        virtual void onChanged();
        virtual void onInvalidated();

    protected:
        SMCListView * m_pOwner;
    };

    //////////////////////////////////////////////////////////////////////////
    void SMCListViewDataSetObserver::onChanged()
    {
        m_pOwner->onDataSetChanged();
    }

    void SMCListViewDataSetObserver::onInvalidated()
    {
        m_pOwner->onDataSetInvalidated();
    }

//////////////////////////////////////////////////////////////////////////
//  SMCListView

    SMCListView::SMCListView()
        :m_nHeaderHeight(25)
        ,m_pHeader(NULL)
        ,m_iSelItem(-1)
        ,m_iFirstVisible(-1)
        ,m_pHoverItem(NULL)
        ,m_itemCapture(NULL)
        ,m_bScrollUpdate(TRUE)
        ,m_pSkinDivider(NULL)
        ,m_nDividerSize(0)
        ,m_nUpdateInterval(40)
    {
        m_bFocusable = TRUE;
        m_bClipClient = TRUE;
        m_observer.Attach(new SMCListViewDataSetObserver(this));

        m_evtSet.addEvent(EVENTID(EventLVSelChanged));
    }

    SMCListView::~SMCListView()
    {
        m_observer=NULL;
        m_lvItemLocator=NULL;
    }


    BOOL SMCListView::SetAdapter(IMcAdapter * adapter)
    {
        if(!m_lvItemLocator)
        {
            SASSERT_FMT(FALSE,_T("error: A item locator is in need before setting adapter!!!"));
            return FALSE;
        }

        if(m_adapter)
        {
            m_adapter->unregisterDataSetObserver(m_observer);

            //free all itemPanels in recycle
            for(size_t i=0;i<m_itemRecycle.GetCount();i++)
            {
                SList<SItemPanel*> *lstItemPanels = m_itemRecycle.GetAt(i);
                SPOSITION pos = lstItemPanels->GetHeadPosition();
                while(pos)
                {
                    SItemPanel * pItemPanel = lstItemPanels->GetNext(pos);
                    pItemPanel->DestroyWindow();
                }
                delete lstItemPanels;
            }
            m_itemRecycle.RemoveAll();

            //free all visible itemPanels
            SPOSITION pos=m_lstItems.GetHeadPosition();
            while(pos)
            {
                ItemInfo ii = m_lstItems.GetNext(pos);
                ii.pItem->DestroyWindow();
            }
            m_lstItems.RemoveAll();
        }

        m_adapter = adapter;
        if(m_lvItemLocator)
            m_lvItemLocator->SetAdapter(adapter);
        if(m_adapter) 
        {
            m_adapter->registerDataSetObserver(m_observer);
            for(int i=0;i<m_adapter->getViewTypeCount();i++)
            {
                m_itemRecycle.Add(new SList<SItemPanel*>());
            }
            onDataSetChanged();
        }
        return TRUE;
    }
    
int SMCListView::InsertColumn(int nIndex, LPCTSTR pszText, int nWidth, LPARAM lParam)
{
    SASSERT(m_pHeader);

    int nRet = m_pHeader->InsertItem(nIndex, pszText, nWidth, ST_NULL, lParam);
    UpdateScrollBar();
    return nRet;
}

BOOL SMCListView::CreateChildren(pugi::xml_node xmlNode)
{
    //  listctrl的子控件只能是一个header控件
    if (!__super::CreateChildren(xmlNode))
        return FALSE;
        
    pugi::xml_node xmlTemplate = xmlNode.child(L"template");
    if(xmlTemplate)
    {
        m_xmlTemplate.append_copy(xmlTemplate);
        int nItemHei = xmlTemplate.attribute(L"itemHeight").as_int(-1);
        if(nItemHei>0)
        {//指定了itemHeight属性时创建一个固定行高的定位器
            IListViewItemLocator * pItemLocator = new  SListViewItemLocatorFix(nItemHei,m_nDividerSize);
            SetItemLocator(pItemLocator);
            pItemLocator->Release();
        }else
        {//创建一个行高可变的行定位器，从defHeight属性中获取默认行高
            IListViewItemLocator * pItemLocator = new  SListViewItemLocatorFlex(xmlTemplate.attribute(L"defHeight").as_int(30),m_nDividerSize);
            SetItemLocator(pItemLocator);
            pItemLocator->Release();
        }
    }

    m_pHeader=NULL;
    
    SWindow *pChild=GetWindow(GSW_FIRSTCHILD);
    while(pChild)
    {
        if(pChild->IsClass(SHeaderCtrl::GetClassName()))
        {
            m_pHeader=(SHeaderCtrl*)pChild;
            break;
        }
        pChild=pChild->GetWindow(GSW_NEXTSIBLING);
    }
    if(!m_pHeader) return FALSE;
        
    SStringW strPos;
    strPos.Format(L"0,0,-0,%d",m_nHeaderHeight);
    m_pHeader->SetAttribute(L"pos",strPos,TRUE);

    m_pHeader->GetEventSet()->subscribeEvent(EventHeaderItemChanging::EventID, Subscriber(&SMCListView::OnHeaderSizeChanging,this));
    m_pHeader->GetEventSet()->subscribeEvent(EventHeaderItemSwap::EventID, Subscriber(&SMCListView::OnHeaderSwap,this));
    m_pHeader->GetEventSet()->subscribeEvent(EventHeaderClick::EventID, Subscriber(&SMCListView::OnHeaderClick,this));

    return TRUE;
}


CRect SMCListView::GetListRect()
{
    CRect rcList;

    GetClientRect(&rcList);
    rcList.top += m_nHeaderHeight;

    return rcList;
}

//  更新滚动条
void SMCListView::UpdateScrollBar()
{
    CSize szView;
    szView.cx = m_pHeader->GetTotalWidth();
    szView.cy = m_lvItemLocator->GetTotalHeight();

    CRect rcClient;
    SWindow::GetClientRect(&rcClient);//不计算滚动条大小
    rcClient.top+=m_nHeaderHeight;
    if(rcClient.bottom<rcClient.top) 
        rcClient.bottom=rcClient.top;
    CSize size = rcClient.Size();
    //  关闭滚动条
    m_wBarVisible = SSB_NULL;

    if (size.cy<szView.cy || (size.cy<szView.cy+m_nSbWid && size.cx<szView.cx))
    {
        //  需要纵向滚动条
        m_wBarVisible |= SSB_VERT;
        m_siVer.nMin  = 0;
        m_siVer.nMax  = szView.cy-1;
        m_siVer.nPage = rcClient.Height();

        if (size.cx-m_nSbWid < szView.cx)
        {
            //  需要横向滚动条
            m_wBarVisible |= SSB_HORZ;

            m_siHoz.nMin  = 0;
            m_siHoz.nMax  = szView.cx-1;
            m_siHoz.nPage = (size.cx-m_nSbWid) > 0 ? (size.cx-m_nSbWid) : 0;
        }
        else
        {
            //  不需要横向滚动条
            m_siHoz.nPage = size.cx;
            m_siHoz.nMin  = 0;
            m_siHoz.nMax  = m_siHoz.nPage-1;
            m_siHoz.nPos  = 0;
        }
    }
    else
    {
        //  不需要纵向滚动条
        m_siVer.nPage = size.cy;
        m_siVer.nMin  = 0;
        m_siVer.nMax  = size.cy-1;
        m_siVer.nPos  = 0;

        if (size.cx < szView.cx)
        {
            //  需要横向滚动条
            m_wBarVisible |= SSB_HORZ;
            m_siHoz.nMin  = 0;
            m_siHoz.nMax  = szView.cx-1;
            m_siHoz.nPage = size.cx;
        }
        else
        {
            //  不需要横向滚动条
            m_siHoz.nPage = size.cx;
            m_siHoz.nMin  = 0;
            m_siHoz.nMax  = m_siHoz.nPage-1;
            m_siHoz.nPos  = 0;
        }
    }

    //  根据需要调整原点位置
    if (HasScrollBar(FALSE) && m_siHoz.nPos+m_siHoz.nPage>szView.cx)
    {
        m_siHoz.nPos = szView.cx-m_siHoz.nPage;
    }

    if (HasScrollBar(TRUE) && m_siVer.nPos +m_siVer.nPage>szView.cy)
    {
        m_siVer.nPos = szView.cy-m_siVer.nPage;
    }
    
    SetScrollPos(TRUE, m_siVer.nPos, TRUE);
    SetScrollPos(FALSE, m_siHoz.nPos, TRUE);

    //  重新计算客户区及非客户区
    SSendMessage(WM_NCCALCSIZE);

    Invalidate();
}

//更新表头位置
void SMCListView::UpdateHeaderCtrl()
{
    CRect rcClient;
    GetClientRect(&rcClient);
    CRect rcHeader(rcClient);
    rcHeader.bottom=rcHeader.top+m_nHeaderHeight;
    rcHeader.left-=m_siHoz.nPos;
    if(m_pHeader) m_pHeader->Move(rcHeader);
}


void SMCListView::DeleteColumn( int iCol )
{
    if(m_pHeader->DeleteItem(iCol))
    {
        UpdateScrollBar();
    }
}


int SMCListView::GetColumnCount() const
{
    if (!m_pHeader)
        return 0;

    return m_pHeader->GetItemCount();
}


void SMCListView::UpdateChildrenPosition()
{
    __super::UpdateChildrenPosition();
    UpdateHeaderCtrl();
}


bool SMCListView::OnHeaderClick(EventArgs *pEvt)
{
    EventHeaderClick *pEvt2 = sobj_cast<EventHeaderClick>(pEvt);
    SASSERT(pEvt2);
    SHDITEM hi;
    hi.mask = SHDI_ORDER|SHDI_SORTFLAG;
    SHDSORTFLAG *pstFlags = new SHDSORTFLAG[m_pHeader->GetItemCount()];
    int *pOrders = new int[m_pHeader->GetItemCount()];
    int iCol = -1;
    for(int i=0;i<m_pHeader->GetItemCount();i++)
    {
        m_pHeader->GetItem(i,&hi);
        pstFlags[hi.iOrder]=hi.stFlag;
        pOrders[hi.iOrder]=i;
        if(i == pEvt2->iItem) iCol = hi.iOrder;
    }
    if(m_adapter->OnSort(iCol,pstFlags,m_pHeader->GetItemCount()))
    {
        //更新表头的排序状态
        for(int i=0;i<m_pHeader->GetItemCount();i++)
        {
           m_pHeader->SetItemSort(pOrders[i],pstFlags[i]);
        }
        onDataSetChanged();
    }
    delete []pOrders;
    delete []pstFlags;
    return true;
}

bool SMCListView::OnHeaderSizeChanging(EventArgs *pEvt)
{
    UpdateScrollBar();
    SPOSITION pos = m_lstItems.GetHeadPosition();
    while(pos)
    {
        ItemInfo ii = m_lstItems.GetNext(pos);
        CRect rcItem = ii.pItem->GetWindowRect();
        rcItem.right = m_pHeader->GetTotalWidth();
        ii.pItem->Move(rcItem);
        CRect rcSubItem(rcItem);
        rcSubItem.right=rcSubItem.left=0;
        for(int i=0;i<m_pHeader->GetItemCount();i++)
        {
            SHDITEM hi;
            hi.mask = SHDI_WIDTH|SHDI_ORDER;
            m_pHeader->GetItem(i,&hi);
            rcSubItem.left = rcSubItem.right;
            rcSubItem.right += hi.cx;
            SStringW strColName=m_adapter->GetColumnName(hi.iOrder);
            SWindow *pCol=ii.pItem->FindChildByName(strColName);
            if(pCol)
            {
                pCol->Move(rcSubItem);
            }
        }
    }

    InvalidateRect(GetListRect());    
    return true;
}

bool SMCListView::OnHeaderSwap(EventArgs *pEvt)
{
    OnHeaderSizeChanging(NULL);
    return true;
}


void SMCListView::onDataSetChanged()
{
    if(!m_adapter) return;
    if(m_lvItemLocator) m_lvItemLocator->OnDataSetChanged();
    UpdateScrollBar();
    UpdateVisibleItems();
}

void SMCListView::onDataSetInvalidated()
{
    UpdateVisibleItems();
}

void SMCListView::OnPaint(IRenderTarget *pRT)
{
    SPainter duiDC;
    BeforePaint(pRT,duiDC);


    int iFirst = m_iFirstVisible;
    if(iFirst!=-1)
    {
        CRect rcClient;
        GetClientRect(&rcClient);
        rcClient.top += m_nHeaderHeight;
        
        pRT->PushClipRect(&rcClient,RGN_AND);

        CRect rcClip,rcInter;
        pRT->GetClipBox(&rcClip);

        int nOffset = m_lvItemLocator->Item2Position(iFirst)-m_siVer.nPos;

        
        SPOSITION pos= m_lstItems.GetHeadPosition();
        int i=0;
        for(;pos;i++)
        {

            ItemInfo ii = m_lstItems.GetNext(pos);
            CRect rcItem = _OnItemGetRect(iFirst+i);
            rcInter.IntersectRect(&rcClip,&rcItem);
            if(!rcInter.IsRectEmpty())
                ii.pItem->Draw(pRT,rcItem);
            rcItem.top = rcItem.bottom;
            rcItem.bottom += m_lvItemLocator->GetDividerSize();
            if(m_pSkinDivider)
            {//绘制分隔线
                m_pSkinDivider->Draw(pRT,rcItem,0);
            }
        }

        pRT->PopClip();
    }
    AfterPaint(pRT,duiDC);
}

BOOL SMCListView::OnScroll(BOOL bVertical,UINT uCode,int nPos)
{
    int nOldPos = bVertical?m_siVer.nPos:m_siHoz.nPos;
    __super::OnScroll(bVertical, uCode, nPos);
    int nNewPos = bVertical?m_siVer.nPos:m_siHoz.nPos;
    if(nOldPos != nNewPos)
    {
        if(bVertical)
            UpdateVisibleItems();
        else
            UpdateHeaderCtrl();
        //加速滚动时UI的刷新
        static DWORD dwTime1=0;
        DWORD dwTime=GetTickCount();
        if(dwTime-dwTime1>=m_nUpdateInterval && m_bScrollUpdate)
        {
            UpdateWindow();
            dwTime1=dwTime;
        }
    }

    return TRUE;
}


void SMCListView::UpdateVisibleItems()
{
    if(!m_adapter) return;
    int iOldFirstVisible = m_iFirstVisible;
    int iOldLastVisible = m_iFirstVisible + m_lstItems.GetCount();
    int nOldTotalHeight = m_lvItemLocator->GetTotalHeight();

    int iNewFirstVisible = m_lvItemLocator->Position2Item(m_siVer.nPos);
    int iNewLastVisible = iNewFirstVisible;
    int pos = m_lvItemLocator->Item2Position(iNewFirstVisible);


    ItemInfo *pItemInfos = new ItemInfo[m_lstItems.GetCount()];
    SPOSITION spos = m_lstItems.GetHeadPosition();
    int i=0;
    while(spos)
    {
        pItemInfos[i++]=m_lstItems.GetNext(spos);
    }

    m_lstItems.RemoveAll();

    if(iNewFirstVisible!=-1)
    {
        while(pos < m_siVer.nPos + (int)m_siVer.nPage && iNewLastVisible < m_adapter->getCount())
        {
            if(iNewLastVisible>=iOldLastVisible && iNewLastVisible < iOldLastVisible)
            {//use the old visible item
                int iItem = iNewLastVisible-(iNewFirstVisible-iOldFirstVisible);
                SASSERT(iItem>=0 && iItem <= (iOldLastVisible-iOldFirstVisible));
                m_lstItems.AddTail(pItemInfos[iItem]);
                pos += m_lvItemLocator->GetItemHeight(iNewLastVisible);
                pItemInfos[iItem].pItem = NULL;//标记该行已经被重用
            }else
            {//create new visible item
                int nItemType = m_adapter->getItemViewType(iNewLastVisible);
                SList<SItemPanel *> *lstRecycle = m_itemRecycle.GetAt(nItemType);

                SItemPanel * pItemPanel = NULL;
                if(lstRecycle->IsEmpty())
                {//创建一个新的列表项
                    pItemPanel = SItemPanel::Create(this,pugi::xml_node(),this);
                }else
                {
                    pItemPanel = lstRecycle->RemoveHead();
                }
                pItemPanel->SetItemIndex(iNewLastVisible);

                CRect rcItem(0,0,m_pHeader->GetTotalWidth(),0);
                if(m_lvItemLocator->IsFixHeight())
                {
                    rcItem.bottom=m_lvItemLocator->GetItemHeight(iNewLastVisible);
                    pItemPanel->Move(rcItem);
                }
                m_adapter->getView(iNewLastVisible,pItemPanel,m_xmlTemplate.first_child());
                
                if(!m_lvItemLocator->IsFixHeight())
                {//根据第一列的高度来设定整行的高度
                    rcItem.bottom=0;
                    SStringW strColName = m_adapter->GetColumnName(0);//获取第一列的name
                    SWindow *pColWnd = pItemPanel->FindChildByName(strColName);
                    if(pColWnd)
                    {
                        CSize szItem = pColWnd->GetDesiredSize(rcItem);
                        rcItem.bottom = rcItem.top + szItem.cy;
                        pItemPanel->Move(rcItem);
                        m_lvItemLocator->SetItemHeight(iNewLastVisible,szItem.cy);                    
                    }
                }
                                
                //调整网格大小
                CRect rcSubItem(rcItem);
                rcSubItem.right=rcSubItem.left;
                for(int i=0;i<m_pHeader->GetItemCount();i++)
                {
                    SHDITEM hditem;
                    hditem.mask=SHDI_ORDER|SHDI_WIDTH;
                    m_pHeader->GetItem(i,&hditem);
                    SStringW strColName = m_adapter->GetColumnName(hditem.iOrder);
                    SWindow *pColWnd=pItemPanel->FindChildByName(strColName);
                    rcSubItem.left = rcSubItem.right;
                    rcSubItem.right += hditem.cx;
                    if(pColWnd)
                    {
                        pColWnd->Move(rcSubItem);
                    }
                }
                pItemPanel->UpdateLayout();
                
                if(iNewLastVisible == m_iSelItem)
                {
                    pItemPanel->ModifyItemState(WndState_Check,0);
                }
                ItemInfo ii;
                ii.nType = nItemType;
                ii.pItem = pItemPanel;
                m_lstItems.AddTail(ii);
                pos += rcItem.bottom + m_lvItemLocator->GetDividerSize();
            }
            iNewLastVisible ++;
        }
    }

    //move old visible items which were not reused to recycle
    for(int i=0;i<(iOldLastVisible-iOldFirstVisible);i++)
    {
        ItemInfo ii = pItemInfos[i];
        if(!ii.pItem) continue;

        if(ii.pItem == m_pHoverItem)
        {
            m_pHoverItem->DoFrameEvent(WM_MOUSELEAVE,0,0);
            m_pHoverItem=NULL;
        }
        if(ii.pItem->GetItemIndex() == m_iSelItem)
        {
            ii.pItem->ModifyItemState(0,WndState_Check);
            ii.pItem->GetFocusManager()->SetFocusedHwnd(0);
        }
        m_itemRecycle[ii.nType]->AddTail(ii.pItem);    
    }
    delete [] pItemInfos;

    m_iFirstVisible = iNewFirstVisible;

    if(!m_lvItemLocator->IsFixHeight() && m_lvItemLocator->GetTotalHeight() != nOldTotalHeight)
    {//update scroll range
        UpdateScrollBar();
        UpdateVisibleItems();//根据新的滚动条状态重新记录显示列表项
    }
}

void SMCListView::OnSize(UINT nType, CSize size)
{
    __super::OnSize(nType,size);
    UpdateScrollBar();
    UpdateHeaderCtrl();

    //update item window
    CRect rcClient=GetClientRect();
    SPOSITION pos = m_lstItems.GetHeadPosition();
    while(pos)
    {
        ItemInfo ii = m_lstItems.GetNext(pos);
        int idx = (int)ii.pItem->GetItemIndex();
        int nHei = m_lvItemLocator->GetItemHeight(idx);
        CRect rcItem(0,0,m_pHeader->GetTotalWidth(),nHei);
        ii.pItem->Move(rcItem);
    }

    UpdateVisibleItems();
}

void SMCListView::OnDestroy()
{
    //destroy all itempanel
    SPOSITION pos = m_lstItems.GetHeadPosition();
    while(pos)
    {
        ItemInfo ii = m_lstItems.GetNext(pos);
        ii.pItem->Release();
    }
    m_lstItems.RemoveAll();

    for(int i=0;i<(int)m_itemRecycle.GetCount();i++)
    {
        SList<SItemPanel*> *pLstTypeItems = m_itemRecycle[i];
        SPOSITION pos = pLstTypeItems->GetHeadPosition();
        while(pos)
        {
            SItemPanel *pItem = pLstTypeItems->GetNext(pos);
            pItem->Release();
        }
        delete pLstTypeItems;
    }
    m_itemRecycle.RemoveAll();
    __super::OnDestroy();
}


//////////////////////////////////////////////////////////////////////////
void SMCListView::OnItemRequestRelayout(SItemPanel *pItem)
{
    pItem->UpdateChildrenPosition();
}

BOOL SMCListView::IsItemRedrawDelay()
{
    return TRUE;
}

BOOL SMCListView::OnItemGetRect(SItemPanel *pItem,CRect &rcItem)
{
    int iPosition = (int)pItem->GetItemIndex();
    rcItem = _OnItemGetRect(iPosition);
    return TRUE;
}

CRect SMCListView::_OnItemGetRect(int iPosition)
{
    int nOffset = m_lvItemLocator->Item2Position(iPosition)-m_siVer.nPos;
    CRect rcItem = GetClientRect();
    rcItem.top += m_nHeaderHeight + nOffset;
    rcItem.bottom = rcItem.top + m_lvItemLocator->GetItemHeight(iPosition);
    rcItem.left -= m_siHoz.nPos;
    rcItem.right = rcItem.left + m_pHeader->GetTotalWidth();
    return rcItem;
}


void SMCListView::OnItemSetCapture(SItemPanel *pItem,BOOL bCapture)
{
    if(bCapture)
    {
        GetContainer()->OnSetSwndCapture(m_swnd);
        m_itemCapture=pItem;
    }else
    {
        GetContainer()->OnReleaseSwndCapture();
        m_itemCapture=NULL;
    }
}

void SMCListView::RedrawItem(SItemPanel *pItem)
{
    pItem->InvalidateRect(NULL);
}

SItemPanel * SMCListView::HitTest(CPoint & pt)
{
    SPOSITION pos = m_lstItems.GetHeadPosition();
    while(pos)
    {
        ItemInfo ii = m_lstItems.GetNext(pos);
        CRect rcItem = ii.pItem->GetItemRect();
        if(rcItem.PtInRect(pt)) 
        {
            pt-=rcItem.TopLeft();
            return ii.pItem;
        }
    }
    return NULL;
}

LRESULT SMCListView::OnMouseEvent(UINT uMsg,WPARAM wParam,LPARAM lParam)
{
    if(!m_adapter)
    {
        SetMsgHandled(FALSE);
        return 0;
    }

    LRESULT lRet=0;
    CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

    if(uMsg == WM_LBUTTONDOWN)
        __super::OnLButtonDown(wParam,pt);
    else if(uMsg == WM_RBUTTONDOWN)
        __super::OnRButtonDown(uMsg, pt);
        
    if(m_itemCapture)
    {
        CRect rcItem=m_itemCapture->GetItemRect();
        pt.Offset(-rcItem.TopLeft());
        lRet = m_itemCapture->DoFrameEvent(uMsg,wParam,MAKELPARAM(pt.x,pt.y));
    }
    else
    {
        if(m_bFocusable && (uMsg==WM_LBUTTONDOWN || uMsg== WM_RBUTTONDOWN || uMsg==WM_LBUTTONDBLCLK))
            SetFocus();

        SItemPanel * pHover=HitTest(pt);
        if(pHover!=m_pHoverItem)
        {
            SItemPanel * nOldHover=m_pHoverItem;
            m_pHoverItem=pHover;
            if(nOldHover)
            {
                nOldHover->DoFrameEvent(WM_MOUSELEAVE,0,0);
                RedrawItem(nOldHover);
            }
            if(m_pHoverItem)
            {
                m_pHoverItem->DoFrameEvent(WM_MOUSEHOVER,wParam,MAKELPARAM(pt.x,pt.y));
                RedrawItem(m_pHoverItem);
            }
        }
        if(uMsg==WM_LBUTTONDOWN )
        {//选择一个新行的时候原有行失去焦点
            SWND hHitWnd = 0;
            int nSelNew = -1;
            if(m_pHoverItem)
            {
                nSelNew = m_pHoverItem->GetItemIndex();
                hHitWnd = m_pHoverItem->SwndFromPoint(pt,FALSE);
            }

            _SetSel(nSelNew,TRUE,hHitWnd);
        }
        if(m_pHoverItem)
        {
            m_pHoverItem->DoFrameEvent(uMsg,wParam,MAKELPARAM(pt.x,pt.y));
        }
    }
    if(uMsg == WM_LBUTTONUP)
        __super::OnLButtonUp(wParam,pt);

    return 0;
}

LRESULT SMCListView::OnKeyEvent(UINT uMsg,WPARAM wParam,LPARAM lParam)
{
    LRESULT lRet=0;
    SItemPanel *pItem = GetItemPanel(m_iSelItem);
    if(pItem)
    {
        lRet=pItem->DoFrameEvent(uMsg,wParam,lParam);
        SetMsgHandled(pItem->IsMsgHandled());
    }else
    {
        SetMsgHandled(FALSE);
    }
    return lRet;
}

void SMCListView::OnMouseLeave()
{
    if(m_pHoverItem)
    {
        m_pHoverItem->DoFrameEvent(WM_MOUSELEAVE,0,0);
        m_pHoverItem = NULL;
    }

}

void SMCListView::OnKeyDown( TCHAR nChar, UINT nRepCnt, UINT nFlags )
{
    if(!m_adapter)
    {
        SetMsgHandled(FALSE);
        return;
    }
    int  nNewSelItem = -1;
    SWindow *pOwner = GetOwner();
    if (pOwner && (nChar == VK_ESCAPE))
    {
        pOwner->SSendMessage(WM_KEYDOWN, nChar, MAKELONG(nFlags, nRepCnt));
        return;
    }

    m_bScrollUpdate=FALSE;
    if (nChar == VK_DOWN && m_iSelItem < m_adapter->getCount() - 1)
        nNewSelItem = m_iSelItem+1;
    else if (nChar == VK_UP && m_iSelItem > 0)
        nNewSelItem = m_iSelItem-1;
    else if (pOwner && nChar == VK_RETURN)
        nNewSelItem = m_iSelItem;
    else if(nChar == VK_PRIOR)
    {
        OnScroll(TRUE,SB_PAGEUP,0);
        if(!m_lstItems.IsEmpty())
        {
            nNewSelItem = m_lstItems.GetHead().pItem->GetItemIndex();
        }
    }else if(nChar == VK_NEXT)
    {
        OnScroll(TRUE,SB_PAGEDOWN,0);
        if(!m_lstItems.IsEmpty())
        {
            nNewSelItem = m_lstItems.GetTail().pItem->GetItemIndex();
        }
    }

    if(nNewSelItem!=-1)
    {
        EnsureVisible(nNewSelItem);
        SetSel(nNewSelItem);
    }
    m_bScrollUpdate=TRUE;
}

void SMCListView::EnsureVisible( int iItem )
{
    if(iItem<0 || iItem>=m_adapter->getCount()) return;

    int iFirstVisible= m_iFirstVisible;
    int iLastVisible = m_iFirstVisible + m_lstItems.GetCount();

    if(iItem>=iFirstVisible && iItem<iLastVisible)
        return;

    int pos = m_lvItemLocator->Item2Position(iItem);

    if(iItem < iFirstVisible)
    {//scroll up
        OnScroll(TRUE,SB_THUMBPOSITION,pos);
    }else // if(iItem >= iLastVisible)
    {//scroll down
        int iTop = iItem;
        int pos2 = pos;
        int topSize = m_siVer.nPage - m_lvItemLocator->GetItemHeight(iItem);
        while(iTop>=0 && (pos - pos2) < topSize)
        {
            pos2 = m_lvItemLocator->Item2Position(--iTop);
        }
        OnScroll(TRUE,SB_THUMBPOSITION,pos2);
    }
}

BOOL SMCListView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    SItemPanel *pSelItem = GetItemPanel(m_iSelItem);
    if(pSelItem)
    {
        CRect rcItem = pSelItem->GetItemRect();
        CPoint pt2=pt-rcItem.TopLeft();
        if(pSelItem->DoFrameEvent(WM_MOUSEWHEEL,MAKEWPARAM(nFlags,zDelta),MAKELPARAM(pt2.x,pt2.y)))
            return TRUE;
    }
    return __super::OnMouseWheel(nFlags, zDelta, pt);
}

int SMCListView::GetScrollLineSize(BOOL bVertical)
{
    return m_lvItemLocator->GetScrollLineSize();
}

SItemPanel * SMCListView::GetItemPanel(int iItem)
{
    if(iItem<0 || iItem>=m_adapter->getCount()) 
        return NULL; 
    SPOSITION pos = m_lstItems.GetHeadPosition();
    while(pos)
    {
        ItemInfo ii = m_lstItems.GetNext(pos);
        if((int)ii.pItem->GetItemIndex() == iItem)
            return ii.pItem;
    }
    return NULL;
}

void SMCListView::SetSel(int iItem,BOOL bNotify/*=FALSE*/)
{
    _SetSel(iItem,bNotify,0);
}

void SMCListView::SetItemLocator(IListViewItemLocator *pItemLocator)
{
    m_lvItemLocator = pItemLocator;
    if(m_lvItemLocator) m_lvItemLocator->SetAdapter(GetAdapter());
    onDataSetChanged();
}

BOOL SMCListView::OnUpdateToolTip(CPoint pt, SwndToolTipInfo & tipInfo)
{
    if(!m_pHoverItem)
        return __super::OnUpdateToolTip(pt,tipInfo);
    return m_pHoverItem->OnUpdateToolTip(pt,tipInfo);
}

void SMCListView::_SetSel(int iItem,BOOL bNotify, SWND hHitWnd)
{
    if(!m_adapter) return;

    if(iItem>=m_adapter->getCount())
        return;

    if(iItem<0) iItem = -1;

    int nOldSel = m_iSelItem;
    int nNewSel = iItem;

    m_iSelItem = nNewSel;
    if(bNotify)
    {
        EventLVSelChanged evt(this);
        evt.iOldSel = nOldSel;
        evt.iNewSel = nNewSel;
        evt.hHitWnd =hHitWnd;
        FireEvent(evt);
        if(evt.bCancel) 
        {//Cancel SetSel and restore selection state
            m_iSelItem = nOldSel;
            return;
        }
    }

    if(nOldSel == nNewSel)
        return;

    m_iSelItem = nOldSel;
    SItemPanel *pItem = GetItemPanel(nOldSel);
    if(pItem)
    {
        pItem->ModifyItemState(0,WndState_Check);
        RedrawItem(pItem);
    }
    m_iSelItem = nNewSel;
    pItem = GetItemPanel(nNewSel);
    if(pItem)
    {
        pItem->ModifyItemState(WndState_Check,0);
        RedrawItem(pItem);
    }
}

}//end of namespace 
