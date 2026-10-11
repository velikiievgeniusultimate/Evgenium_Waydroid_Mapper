#include "SkillCircleDetector.h"
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double Pi = 3.14159265358979323846;
struct Edge {
    int x, y;
    double dx, dy;
};
struct Candidate {
    double x, y, r, score;
};
}

static SkillCircleResult detectSkillCircleCoarse(
    const QImage& source, const QImage& baseline, QPointF anchor, bool fullScreen,
    std::vector<SkillCircleResult> *possibilities)
{
    if (source.isNull() || source.width() < 48 || source.height() < 48)
        return { };
    const double scale = std::min(1.0, 960.0 / std::max(source.width(), source.height()));
    const QImage image = source
                             .scaled(std::max(1, qRound(source.width() * scale)),
                                 std::max(1, qRound(source.height() * scale)),
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    const QImage before = baseline.isNull()
        ? QImage()
        : baseline.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height(), size = w * h;
    anchor *= scale;
    const int minR = std::max(12, qRound(std::min(w, h) * .025));
    const int maxR = qRound(std::min(w, h) * .46);
    const double nearDistance = std::max(100.0, std::min(w, h) * .28);
    std::vector<float> blue(size), smooth(size), strength(size);
    std::vector<unsigned char> changed(size, 1);
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        const auto* old
            = before.isNull() ? nullptr : reinterpret_cast<const QRgb*>(before.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            blue[y * w + x] = std::max(0, std::min(qBlue(p) - qRed(p), (qBlue(p) - qGreen(p)) * 2));
            if (old)
                changed[y * w + x] = std::abs(qRed(p) - qRed(old[x]))
                        + std::abs(qGreen(p) - qGreen(old[x])) + std::abs(qBlue(p) - qBlue(old[x]))
                    > 30;
        }
    }
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
            double sum = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    sum += blue[(y + dy) * w + x + dx];
            smooth[y * w + x] = sum / 9;
        }
    std::vector<Edge> edges;
    for (int y = 2; y < h - 2; ++y)
        for (int x = 2; x < w - 2; ++x) {
            const double dx = smooth[y * w + x + 1] - smooth[y * w + x - 1];
            const double dy = smooth[(y + 1) * w + x] - smooth[(y - 1) * w + x];
            const double length = std::hypot(dx, dy);
            if (length < 8 || smooth[y * w + x] < 12)
                continue;
            strength[y * w + x] = std::min(1.0, length / 24.0);
            edges.push_back({ x, y, dx / length, dy / length });
        }
    // Gradient Hough voting: edge normals point towards a circle's centre.
    std::vector<int> votes(size);
    const int stride = std::max(1, static_cast<int>(edges.size()) / 35000);
    for (size_t i = 0; i < edges.size(); i += stride) {
        const auto& e = edges[i];
        for (int r = minR; r <= maxR; r += 2)
            for (int sign : { -1, 1 }) {
                int x = qRound(e.x + sign * e.dx * r), y = qRound(e.y + sign * e.dy * r);
                if (x < 0 || y < 0 || x >= w || y >= h)
                    continue;
                if (!fullScreen && std::hypot(x - anchor.x(), y - anchor.y()) > nearDistance)
                    continue;
                ++votes[y * w + x];
            }
    }
    std::vector<std::pair<int, int>> peaks;
    for (int y = 2; y < h - 2; y += 2)
        for (int x = 2; x < w - 2; x += 2) {
            int v = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    v += votes[(y + dy) * w + x + dx];
            if (v >= 10)
                peaks.emplace_back(v, y * w + x);
        }
    std::sort(peaks.begin(), peaks.end(), [](auto a, auto b) { return a.first > b.first; });
    std::vector<QPoint> centers;
    for (auto [value, index] : peaks) {
        const QPoint p(index % w, index / w);
        if (std::any_of(centers.begin(), centers.end(),
                [&](QPoint c) { return QLineF(p, c).length() < 8; }))
            continue;
        centers.push_back(p);
        if (centers.size() >= 32)
            break;
    }
    // Require evidence around most of the outer circumference and several
    // quadrants. The smaller thumb follows the finger and is NOT a centre cue.
    auto score = [&](double cx, double cy, double r, bool requireChange) {
        int seen = 0, hits = 0, moving = 0;
        double sum = 0;
        int quadrants[4] = { };
        for (int i = 0; i < 96; ++i) {
            double a = 2 * Pi * i / 96, co = std::cos(a), si = std::sin(a), best = 0;
            bool change = false;
            int px = qRound(cx + r * co), py = qRound(cy + r * si);
            if (px < 3 || py < 3 || px >= w - 3 || py >= h - 3)
                continue;
            ++seen;
            for (int dr = -2; dr <= 2; ++dr) {
                int x = qRound(cx + (r + dr) * co), y = qRound(cy + (r + dr) * si);
                if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
                    continue;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int k = (y + dy) * w + x + dx;
                        best = std::max(best, static_cast<double>(strength[k]));
                        change |= changed[k];
                    }
            }
            if (best > .25) {
                ++hits;
                ++quadrants[i / 24];
            }
            if (change)
                ++moving;
            sum += best;
        }
        if (seen < 58 || hits < seen * .64)
            return 0.0;
        if (requireChange && !before.isNull() && moving < seen * .45)
            return 0.0;
        int covered = 0;
        for (int n : quadrants)
            covered += n >= 8;
        if (covered < 3)
            return 0.0;
        return .65 * hits / seen + .35 * sum / seen;
    };
    std::vector<Candidate> candidates;
    for (QPoint c : centers)
        for (int r = minR; r <= maxR; ++r) {
            const double outer = score(c.x(), c.y(), r, true);
            if (outer < .63)
                continue;
            // Prefer the outer boundary over the much smaller movable thumb.
            candidates.push_back(
                { double(c.x()), double(c.y()), double(r), outer + .12 * r / maxR });
        }
    if (candidates.empty())
        return { };
    auto best = *std::max_element(
        candidates.begin(), candidates.end(), [](auto a, auto b) { return a.score < b.score; });
    // A sharp thumb can outscore a partly obscured outer rim. Prefer a
    // supported larger concentric rim over that contained thumb candidate.
    const auto strongest = best;
    for (const auto &c : candidates)
        if (c.r > strongest.r*2 && c.r < strongest.r*5
            && c.score > strongest.score-.20
            && std::hypot(c.x-strongest.x,c.y-strongest.y) < c.r*.12
            && c.r > best.r)
            best=c;
    if (possibilities) {
        auto ranked=candidates;
        std::sort(ranked.begin(),ranked.end(),[](auto a,auto b){return a.score>b.score;});
        ranked.insert(ranked.begin(),best);
        std::vector<Candidate> distinct;
        for(const auto &c:ranked) {
            if(std::any_of(distinct.begin(),distinct.end(),[&](auto d) {
                return std::hypot(c.x-d.x,c.y-d.y)<std::max(8.0,c.r*.10)
                    && std::abs(c.r-d.r)<std::max(5.0,c.r*.20);
            })) continue;
            distinct.push_back(c);
            possibilities->push_back({true,{c.x/scale,c.y/scale},c.r/scale,c.score});
            if(distinct.size()>=16) break;
        }
    }
    // Distinct equally convincing new circles are ambiguous. Contained rings
    // belong to the same joystick (including a displaced movable thumb).
    for (const auto& other : candidates) {
        const double distance = std::hypot(other.x - best.x, other.y - best.y);
        if (!possibilities && other.score > best.score - .04 && distance > std::abs(other.r - best.r) + 8)
            return { };
    }
    // Refine sub-grid centre and outer radius locally.
    const auto base = best;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
            for (int dr = -2; dr <= 2; ++dr) {
                const double r = base.r + dr, outer = score(base.x + dx, base.y + dy, r, true);
                if (outer >= .63 && outer + .12 * r / maxR > best.score)
                    best = { base.x + dx, base.y + dy, r, outer + .12 * r / maxR };
            }
    return { true, QPointF(best.x / scale, best.y / scale), best.r / scale,
        std::clamp(best.score - .12 * best.r / maxR, 0.0, 1.0) };
}

SkillCircleResult detectSkillCircle(const QImage &source, const QImage &before,
                                    QPointF anchor, bool fullScreen)
{
    return detectSkillCircleCoarse(source,before,anchor,fullScreen,nullptr);
}

namespace {
struct RimFit {
    bool valid = false;
    QPointF center;
    double radius = 0, rms = 0, coverage = 0;
};

// Bilinear sampling preserves fractional contour positions. The blue excess
// suppresses warm HUD decorations; no resizing of the measurement region.
double blueAt(const QImage &im, double x, double y)
{
    if (x < 1 || y < 1 || x >= im.width()-2 || y >= im.height()-2) return 0;
    const int ix = int(x), iy = int(y);
    const double fx = x-ix, fy = y-iy;
    auto value = [&](int px, int py) {
        auto p = reinterpret_cast<const QRgb *>(im.constScanLine(py))[px];
        return double(std::max(0, std::min(qBlue(p)-qRed(p), 2*(qBlue(p)-qGreen(p)))));
    };
    return (1-fy)*((1-fx)*value(ix,iy)+fx*value(ix+1,iy))
        + fy*((1-fx)*value(ix,iy+1)+fx*value(ix+1,iy+1));
}

double rimResponse(const QImage &im, QPointF c, double r, double co, double si)
{
    // Outside-facing edge of the blue rim, rather than the filled disc or icon.
    return blueAt(im,c.x()+(r-1)*co,c.y()+(r-1)*si)
         - blueAt(im,c.x()+(r+1)*co,c.y()+(r+1)*si);
}

bool solve3(double a[3][4], double result[3])
{
    for (int k=0;k<3;++k) {
        int pivot=k;
        for(int i=k+1;i<3;++i) if(std::abs(a[i][k])>std::abs(a[pivot][k])) pivot=i;
        if(std::abs(a[pivot][k])<1e-9) return false;
        for(int j=k;j<4;++j) std::swap(a[k][j],a[pivot][j]);
        const double d=a[k][k];for(int j=k;j<4;++j) a[k][j]/=d;
        for(int i=0;i<3;++i) if(i!=k) {
            const double m=a[i][k];for(int j=k;j<4;++j) a[i][j]-=m*a[k][j];
        }
    }
    for(int i=0;i<3;++i) result[i]=a[i][3];
    return true;
}

RimFit fitRim(const QImage &im, QPointF seed, double radius, double band)
{
    QPointF c=seed;
    double r=radius;
    std::vector<QPointF> points;
    // Re-extract the edge after each fit. Huber weights reject texture, symbols,
    // and arrows; equal angular sampling prevents one bright arc dominating.
    for(int pass=0;pass<6;++pass) {
        points.clear();
        for(int i=0;i<360;++i) {
            double a=2*Pi*i/360,co=std::cos(a),si=std::sin(a);
            double best=0,br=0;
            const double reach=pass==0 ? band : std::max(2.5,band*.4);
            for(double rr=std::max(4.0,r-reach);rr<=r+reach;rr+=.5) {
                const double x=c.x()+rr*co,y=c.y()+rr*si;
                if(x<3||y<3||x>=im.width()-3||y>=im.height()-3) continue;
                const double response=rimResponse(im,c,rr,co,si);
                if(response>best) {best=response;br=rr;}
            }
            if(best<3) continue;
            const double left=rimResponse(im,c,br-.5,co,si);
            const double right=rimResponse(im,c,br+.5,co,si);
            const double denominator=left-2*best+right;
            if(denominator<-.001) br+=std::clamp(.25*(left-right)/denominator,-.5,.5);
            points.emplace_back(c.x()+br*co,c.y()+br*si);
        }
        if(points.size()<220) return {};
        for(int iteration=0;iteration<12;++iteration) {
            double normal[3][4]={};
            for(auto p:points) {
                const double dx=c.x()-p.x(),dy=c.y()-p.y(),d=std::hypot(dx,dy);
                if(d<1) continue;
                const double residual=d-r;
                const double weight=std::min(1.0,1.0/std::max(.001,std::abs(residual)));
                const double j[3]={dx/d,dy/d,-1};
                for(int k=0;k<3;++k) {
                    for(int l=0;l<3;++l) normal[k][l]+=weight*j[k]*j[l];
                    normal[k][3]-=weight*j[k]*residual;
                }
            }
            double step[3];if(!solve3(normal,step)) return {};
            c+=QPointF(step[0],step[1]);r+=step[2];
            if(QLineF(c,seed).length()>band*2 || r<4 || std::abs(r-radius)>band*2) return {};
            if(std::hypot(step[0],step[1])+std::abs(step[2])<.0001) break;
        }
    }
    int inliers=0,quadrants[4]={};double squared=0;
    const double limit=std::max(1.2,r*.008);
    for(auto p:points) {
        const double error=QLineF(c,p).length()-r;
        if(std::abs(error)>limit) continue;
        ++inliers;squared+=error*error;
        const double a=std::atan2(p.y()-c.y(),p.x()-c.x())+Pi;
        ++quadrants[std::min(3,int(a/(Pi*.5)))];
    }
    for(int q:quadrants) if(q<35) return {};
    if(inliers<230) return {};
    return {true,c,r,std::sqrt(squared/inliers),inliers/360.0};
}
}

static SkillCircleResult fitStationaryCandidate(const QImage &image,
                                                const SkillCircleResult &coarse)
{
    const double band=std::max(5.0,std::max(image.width(),image.height())/960.0*5);
    const auto outer=fitRim(image,coarse.center,coarse.radius,band);
    if(!outer.valid) return {};
    // Find the smaller rim in the expected size interval, independently from
    // its later fit. Never use the central decorative diamond as the centre.
    double best=0,smallRadius=0;
    for(double r=outer.radius*.20;r<outer.radius*.43;r+=.5) {
        int hits=0;double sum=0;
        for(int i=0;i<120;++i) {
            const double a=2*Pi*i/120;
            const double response=rimResponse(image,outer.center,r,std::cos(a),std::sin(a));
            hits+=response>3;sum+=std::clamp(response,0.0,15.0);
        }
        const double score=hits+sum/15;
        if(score>best) {best=score;smallRadius=r;}
    }
    if(best<100) return {};
    const auto thumb=fitRim(image,outer.center,smallRadius,std::max(4.0,outer.radius*.06));
    if(!thumb.valid || QLineF(thumb.center,outer.center).length()>std::max(2.0,outer.radius*.015))
        return {};
    // The stationary thumb is the primary origin. Outer rim is an independent
    // agreement check, not a constraint which could drag that origin away.
    // Measure the outer radius once again with this origin fixed.
    std::vector<double> radii;
    for(int i=0;i<360;++i) {
        const double a=2*Pi*i/360,co=std::cos(a),si=std::sin(a);
        double response=0,br=0;
        for(double r=outer.radius-3;r<=outer.radius+3;r+=.25) {
            double v=rimResponse(image,thumb.center,r,co,si);
            if(v>response) {response=v;br=r;}
        }
        if(response>3) radii.push_back(br);
    }
    if(radii.size()<230) return {};
    std::sort(radii.begin(),radii.end());
    return {true,thumb.center,radii[radii.size()/2],std::min(thumb.coverage,outer.coverage),
        thumb.rms,thumb.radius};
}

SkillCircleResult detectStationarySkillCircle(
    const QImage &source, const QImage &before, QPointF anchor, bool fullScreen)
{
    std::vector<SkillCircleResult> candidates;
    if(!detectSkillCircleCoarse(source,before,anchor,fullScreen,&candidates).found) return {};
    const auto image=source.convertToFormat(QImage::Format_RGB32);
    SkillCircleResult result;
    for(const auto &candidate:candidates) {
        // The movable thumb can itself contain another circular decoration.
        // It is never the outer joystick when a supported larger rim contains
        // it, even when that thumb is displaced and the larger pair is invalid.
        if(std::any_of(candidates.begin(),candidates.end(),[&](const auto &outer) {
            return outer.radius>candidate.radius*1.8 && outer.radius<candidate.radius*6
                && QLineF(outer.center,candidate.center).length()+candidate.radius
                    <outer.radius+std::max(5.0,outer.radius*.05);
        })) continue;
        const auto fitted=fitStationaryCandidate(image,candidate);
        if(!fitted.found) continue;
        // Ambiguity is meaningful only between fully verified concentric pairs,
        // not between the joystick and unrelated round HUD buttons.
        if(result.found && (QLineF(result.center,fitted.center).length()>2
            || std::abs(result.radius-fitted.radius)>2)) return {};
        if(!result.found || fitted.centerResidual<result.centerResidual) result=fitted;
    }
    return result;
}

SkillCircleResult detectUranusCenter(const QImage &source, const QImage &baseline,
                                    QPointF approximateCenter, bool fullScreen)
{
    if (source.isNull() || source.width() < 48 || source.height() < 48) return {};
    const double scale = std::min(1.0, 1280.0 / std::max(source.width(), source.height()));
    const QImage im = source.scaled(std::max(1,qRound(source.width()*scale)),
        std::max(1,qRound(source.height()*scale)), Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    const QImage before = baseline.isNull() ? QImage() : baseline.scaled(im.size(),
        Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    const int w=im.width(), h=im.height();
    approximateCenter *= scale;
    auto pixel=[&](int x,int y) { return reinterpret_cast<const QRgb *>(im.constScanLine(y))[x]; };
    auto excess=[](QRgb p) { return std::min(qGreen(p),qBlue(p))-qRed(p); };
    std::vector<unsigned char> dots(w*h), seen(w*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        auto p=pixel(x,y);
        dots[y*w+x]=qGreen(p)>220 && qBlue(p)>225 && excess(p)>45;
    }
    std::vector<SkillCircleResult> results;
    const int maxDot=std::max(7,qRound(std::min(w,h)*.025));
    for(int y=1;y<h-1;++y) for(int x=1;x<w-1;++x) {
        int start=y*w+x;
        if(!dots[start] || seen[start]) continue;
        std::vector<int> component{start};seen[start]=1;
        double sx=0,sy=0;int minx=x,maxx=x,miny=y,maxy=y;
        for(size_t i=0;i<component.size();++i) {
            int k=component[i],px=k%w,py=k/w;sx+=px;sy+=py;
            minx=std::min(minx,px);maxx=std::max(maxx,px);miny=std::min(miny,py);maxy=std::max(maxy,py);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int nx=px+dx,ny=py+dy;
                if(nx<0||ny<0||nx>=w||ny>=h) continue;
                int n=ny*w+nx;
                if(dots[n]&&!seen[n]) { seen[n]=1;component.push_back(n); }
            }
        }
        const int cw=maxx-minx+1,ch=maxy-miny+1;
        if(component.size()<2||cw>maxDot||ch>maxDot||cw>ch*3||ch>cw*2) continue;
        const QPointF center(sx/component.size(),sy/component.size());
        if(!fullScreen && QLineF(center,approximateCenter).length()>std::min(w,h)*.24) continue;
        if(!before.isNull()) {
            int changed=0;
            for(int k:component) {auto p=pixel(k%w,k/w),old=before.pixel(k%w,k/w);
                changed+=std::abs(qGreen(p)-qGreen(old))+std::abs(qBlue(p)-qBlue(old))+std::abs(qRed(p)-qRed(old))>45;}
            if(changed<component.size()*.5) continue;
        }
        double best=0,bestR=0;
        const int maxR=std::min(180,qRound(std::min(w,h)*.32));
        for(int rx=12;rx<=maxR;rx+=2) for(double ratio:{.42,.48,.54,.60,.66,.72}) {
            int hits=0,visible=0;int quadrants[4]={};
            for(int a=0;a<64;++a) {
                double co=std::cos(a*2*Pi/64),si=std::sin(a*2*Pi/64);bool hit=false;
                int px=qRound(center.x()+rx*co),py=qRound(center.y()+rx*ratio*si);
                if(px<5||py<5||px>=w-5||py>=h-5) continue;
                ++visible;
                for(int dr=-2;dr<=2;++dr) {
                    int ax=qRound(center.x()+(rx+dr)*co),ay=qRound(center.y()+(rx+dr)*ratio*si);
                    int bx=qRound(center.x()+(rx+dr+4)*co),by=qRound(center.y()+(rx+dr+4)*ratio*si);
                    if(ax<0||ay<0||ax>=w||ay>=h||bx<0||by<0||bx>=w||by>=h) continue;
                    auto p=pixel(ax,ay),out=pixel(bx,by);
                    hit|=excess(p)>45 && std::min(qGreen(p),qBlue(p))>175
                        && (excess(p)-excess(out)>14 || std::min(qGreen(p),qBlue(p))-std::min(qGreen(out),qBlue(out))>25);
                }
                if(hit) {++hits;++quadrants[a/16];}
            }
            int covered=0;for(int q:quadrants) covered+=q>=5;
            const double score=visible ? double(hits)/visible : 0;
            if(visible>=48 && covered>=3 && score>best) {best=score;bestR=rx;}
        }
        if(best>=.58) results.push_back({true,center/scale,bestR/scale,best});
    }
    if(results.empty()) return {};
    std::sort(results.begin(),results.end(),[](auto a,auto b){return a.confidence>b.confidence;});
    if(results.size()>1 && results[1].confidence>results[0].confidence-.08
        && QLineF(results[1].center,results[0].center).length()>6/scale) return {};
    // Refine the dot at original Android resolution, avoiding downscale rounding.
    auto result=results.front();
    if(scale<1) {
        double sx=0,sy=0,weight=0;
        const int reach=std::max(3,qRound(3/scale));
        for(int y=std::max(0,qRound(result.center.y())-reach);y<std::min(source.height(),qRound(result.center.y())+reach+1);++y)
            for(int x=std::max(0,qRound(result.center.x())-reach);x<std::min(source.width(),qRound(result.center.x())+reach+1);++x) {
                auto p=source.pixel(x,y);
                if(qGreen(p)>220&&qBlue(p)>225&&excess(p)>45) {sx+=x;sy+=y;++weight;}
            }
        if(weight) result.center={sx/weight,sy/weight};
    }
    return result;
}
